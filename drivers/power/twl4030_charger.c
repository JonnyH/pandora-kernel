/*
 * TWL4030/TPS65950 BCI (Battery Charger Interface) driver
 *
 * Copyright (C) 2010 Gražvydas Ignotas <notasas@gmail.com>
 *
 * based on twl4030_bci_battery.c by TI
 * Copyright (C) 2008 Texas Instruments, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/i2c/twl.h>
#include <linux/power_supply.h>
#include <linux/notifier.h>
#include <linux/usb/otg.h>
#include <linux/ratelimit.h>
#include <linux/regulator/machine.h>
#include <linux/leds.h>

#define TWL4030_BCIMSTATEC	0x02
#define TWL4030_BCIICHG		0x08
#define TWL4030_BCIVAC		0x0a
#define TWL4030_BCIVBUS		0x0c
#define TWL4030_BCIMFSTS3	0x0f
#define TWL4030_BCIMFSTS4	0x10
#define TWL4030_BCIMFKEY	0x11
#define TWL4030_BCICTL1		0x23
#define TWL4030_BCIIREF1	0x27
#define TWL4030_BCIIREF2	0x28

#define TWL4030_BCIAUTOWEN	BIT(5)
#define TWL4030_CONFIG_DONE	BIT(4)
#define TWL4030_CVENAC		BIT(2)
#define TWL4030_BCIAUTOUSB	BIT(1)
#define TWL4030_BCIAUTOAC	BIT(0)
#define TWL4030_CGAIN		BIT(5)
#define TWL4030_USBFASTMCHG	BIT(2)
#define TWL4030_STS_VBUS	BIT(7)
#define TWL4030_STS_USB_ID	BIT(2)
#define TWL4030_STS_CHG		BIT(1)

/* BCI interrupts */
#define TWL4030_WOVF		BIT(0) /* Watchdog overflow */
#define TWL4030_TMOVF		BIT(1) /* Timer overflow */
#define TWL4030_ICHGHIGH	BIT(2) /* Battery charge current high */
#define TWL4030_ICHGLOW		BIT(3) /* Battery cc. low / FSM state change */
#define TWL4030_ICHGEOC		BIT(4) /* Battery current end-of-charge */
#define TWL4030_TBATOR2		BIT(5) /* Battery temperature out of range 2 */
#define TWL4030_TBATOR1		BIT(6) /* Battery temperature out of range 1 */
#define TWL4030_BATSTS		BIT(7) /* Battery status */

#define TWL4030_VBATLVL		BIT(0) /* VBAT level */
#define TWL4030_VBATOV		BIT(1) /* VBAT overvoltage */
#define TWL4030_VBUSOV		BIT(2) /* VBUS overvoltage */
#define TWL4030_ACCHGOV		BIT(3) /* Ac charger overvoltage */

#define TWL4030_MSTATEC_USB		BIT(4)
#define TWL4030_MSTATEC_AC		BIT(5)
#define TWL4030_MSTATEC_MASK		0x0f
#define TWL4030_MSTATEC_QUICK1		0x02
#define TWL4030_MSTATEC_QUICK7		0x07
#define TWL4030_MSTATEC_COMPLETE1	0x0b
#define TWL4030_MSTATEC_COMPLETE4	0x0e

#define TWL4030_KEY_IIREF		0xe7
#define TWL4030_BATSTSMCHG		BIT(6)

#define IRQ_CHECK_PERIOD	(3 * HZ)
#define IRQ_CHECK_THRESHOLD	4

static bool allow_usb = 1;
module_param(allow_usb, bool, 0644);
MODULE_PARM_DESC(allow_usb, "Allow USB charge drawing default current");

struct twl4030_bci {
	struct device		*dev;
	struct power_supply	ac;
	struct power_supply	usb;
	struct otg_transceiver	*transceiver;
	struct notifier_block	otg_nb;
	struct work_struct	work;
	int			irq_chg;
	int			irq_bci;
	bool			ac_charge_enable;
	bool			usb_charge_enable;
	int			usb_current;
	int			ac_current;
	enum power_supply_type	current_supply;
	struct regulator	*usb_reg;
	int			usb_enabled;
	int			irq_had_charger;

	unsigned long		irq_check_count_time;
	int 			irq_check_count;
	int 			irq_check_ac_disabled;

	struct led_trigger	*charging_any_trig;
	int			was_charging_any;

	unsigned long		event;
	struct ratelimit_state	ratelimit;
};

/*
 * clear and set bits on an given register on a given module
 */
static int twl4030_clear_set(u8 mod_no, u8 clear, u8 set, u8 reg)
{
	u8 val = 0;
	int ret;

	ret = twl_i2c_read_u8(mod_no, &val, reg);
	if (ret)
		return ret;

	val &= ~clear;
	val |= set;

	return twl_i2c_write_u8(mod_no, val, reg);
}

static int twl4030_bci_read(u8 reg, u8 *val)
{
	return twl_i2c_read_u8(TWL4030_MODULE_MAIN_CHARGE, val, reg);
}

static int twl4030_bci_write(u8 reg, u8 val)
{
	return twl_i2c_write_u8(TWL4030_MODULE_MAIN_CHARGE, val, reg);
}

static int twl4030_clear_set_boot_bci(u8 clear, u8 set)
{
	return twl4030_clear_set(TWL4030_MODULE_PM_MASTER, clear,
			TWL4030_CONFIG_DONE | TWL4030_BCIAUTOWEN | set,
			TWL4030_PM_MASTER_BOOT_BCI);
}

static int twl4030bci_read_adc_val(u8 reg)
{
	int ret, temp;
	u8 val;

	/* read MSB */
	ret = twl4030_bci_read(reg + 1, &val);
	if (ret)
		return ret;

	temp = (int)(val & 0x03) << 8;

	/* read LSB */
	ret = twl4030_bci_read(reg, &val);
	if (ret)
		return ret;

	return temp | val;
}

/*
 * Check if VBUS power is present
 */
static int twl4030_bci_have_vbus(struct twl4030_bci *bci)
{
	int ret;
	u8 hwsts;

	ret = twl_i2c_read_u8(TWL4030_MODULE_PM_MASTER, &hwsts,
			      TWL4030_PM_MASTER_STS_HW_CONDITIONS);
	if (ret < 0)
		return 0;

	dev_dbg(bci->dev, "check_vbus: HW_CONDITIONS %02x\n", hwsts);

	/* in case we also have STS_USB_ID, VBUS is driven by TWL itself */
	if ((hwsts & TWL4030_STS_VBUS) && !(hwsts & TWL4030_STS_USB_ID))
		return 1;

	return 0;
}

/*
 * Enable/Disable USB Charge functionality.
 */
static int twl4030_charger_enable_usb(struct twl4030_bci *bci, bool enable)
{
	int ret;

	if (enable) {
		if (!bci->usb_charge_enable)
			return -EACCES;

		/* Check for USB charger conneted */
		if (!twl4030_bci_have_vbus(bci))
			return -ENODEV;

		/*
		 * Until we can find out what current the device can provide,
		 * require a module param to enable USB charging.
		 */
		if (!allow_usb) {
			dev_warn(bci->dev, "USB charging is disabled.\n");
			return -EACCES;
		}

		/* Need to keep regulator on */
		if (!bci->usb_enabled &&
		    bci->usb_reg &&
		    regulator_enable(bci->usb_reg) == 0)
			bci->usb_enabled = 1;

		/* forcing the field BCIAUTOUSB (BOOT_BCI[1]) to 1 */
		ret = twl4030_clear_set_boot_bci(0, TWL4030_BCIAUTOUSB);
		if (ret < 0)
			return ret;

		/* forcing USBFASTMCHG(BCIMFSTS4[2]) to 1 */
		ret = twl4030_clear_set(TWL4030_MODULE_MAIN_CHARGE, 0,
			TWL4030_USBFASTMCHG, TWL4030_BCIMFSTS4);
	} else {
		ret = twl4030_clear_set_boot_bci(TWL4030_BCIAUTOUSB, 0);
		if (bci->usb_enabled &&
		    regulator_disable(bci->usb_reg) == 0)
			bci->usb_enabled = 0;
	}

	return ret;
}

/*
 * Enable/Disable AC Charge funtionality.
 */
static int twl4030_charger_enable_ac(bool enable)
{
	int ret;

	if (enable)
		ret = twl4030_clear_set_boot_bci(0, TWL4030_BCIAUTOAC);
	else
		ret = twl4030_clear_set_boot_bci(TWL4030_BCIAUTOAC, 0);

	return ret;
}

static int set_charge_current(struct twl4030_bci *bci, int new_current)
{
	u8 val, boot_bci_prev, cgain_set, cgain_clear;
	int ret, ret2;

	ret = twl4030_bci_read(TWL4030_BCIMFSTS3, &val);
	if (ret)
		goto out_norestore;

	if (!(val & TWL4030_BATSTSMCHG)) {
		dev_err(bci->dev, "missing battery, can't change charge_current\n");
		goto out_norestore;
	}

	ret = twl_i2c_read_u8(TWL4030_MODULE_PM_MASTER, &boot_bci_prev,
		TWL4030_PM_MASTER_BOOT_BCI);
	if (ret)
		goto out_norestore;

	/* 
	 * Stop automatic charging here, because charge current change
	 * requires multiple register writes and CGAIN change requires
	 * automatic charge to be stopped (and CV mode disabled too).
	 */
	ret = twl4030_clear_set_boot_bci(
		TWL4030_CVENAC | TWL4030_BCIAUTOAC | TWL4030_BCIAUTOUSB, 0);
	if (ret)
		goto out;

	ret = twl4030_bci_write(TWL4030_BCIMFKEY, TWL4030_KEY_IIREF);
	if (ret)
		goto out;

	ret = twl4030_bci_write(TWL4030_BCIIREF1, new_current & 0xff);
	if (ret)
		goto out;

	ret = twl4030_bci_write(TWL4030_BCIMFKEY, TWL4030_KEY_IIREF);
	if (ret)
		goto out;

	ret = twl4030_bci_write(TWL4030_BCIIREF2, (new_current >> 8) & 0x1);
	if (ret)
		goto out;

	/* Set CGAIN = 0 or 1 */
	if (new_current > 511) {
		cgain_set = TWL4030_CGAIN;
		cgain_clear = 0;
	} else {
		cgain_set = 0;
		cgain_clear = TWL4030_CGAIN;
	}

	ret = twl4030_clear_set(TWL4030_MODULE_MAIN_CHARGE,
			cgain_clear, cgain_set, TWL4030_BCICTL1);
	if (ret)
		goto out;

	ret = twl4030_bci_read(TWL4030_BCICTL1, &val);
	if (ret != 0 || (val & TWL4030_CGAIN) != cgain_set) {
		dev_err(bci->dev, "CGAIN change failed\n");
		goto out;
	}

out:
	ret2 = twl_i2c_write_u8(TWL4030_MODULE_PM_MASTER, boot_bci_prev,
		TWL4030_PM_MASTER_BOOT_BCI);
	if (ret2 != 0)
		dev_err(bci->dev, "failed boot_bci restore: %d\n", ret2);

out_norestore:
	if (ret != 0)
		dev_err(bci->dev, "charge current change failed: %d\n", ret);

	return ret;
}

/*
 * TWL4030 CHG_PRES (AC charger presence) events
 */
static irqreturn_t twl4030_charger_interrupt(int irq, void *arg)
{
	struct twl4030_bci *bci = arg;
	int have_charger;
	u8 hw_cond;
	int ret;

	ret = twl_i2c_read_u8(TWL4030_MODULE_PM_MASTER, &hw_cond,
			      TWL4030_PM_MASTER_STS_HW_CONDITIONS);
	if (ret < 0) {
		dev_err(bci->dev, "HW_CONDITIONS read failed: %d\n", ret);
		goto out;
	}

	have_charger = (hw_cond & TWL4030_STS_CHG) ? 1 : 0;
	if (have_charger == bci->irq_had_charger)
		goto out;
	bci->irq_had_charger = have_charger;

	dev_dbg(bci->dev, "CHG_PRES irq, hw_cond %02x\n", hw_cond);

	/*
	 * deal with rare mysterious issue of CHG_PRES changing states at ~4Hz
	 * without any charger connected or anything
	 */
	if (time_before(jiffies, bci->irq_check_count_time + IRQ_CHECK_PERIOD)) {
		bci->irq_check_count++;
		if (have_charger && bci->irq_check_count > IRQ_CHECK_THRESHOLD) {
			dev_err(bci->dev, "spurious CHG_PRES irqs detected (%d), disabling charger\n",
				bci->irq_check_count);
			twl4030_charger_enable_ac(false);
			bci->irq_check_ac_disabled = true;
		}
	} else {
		bci->irq_check_count_time = jiffies;
		bci->irq_check_count = 1;
		if (have_charger && bci->irq_check_ac_disabled) {
			twl4030_charger_enable_ac(true);
			bci->irq_check_ac_disabled = false;
		}
	}

	power_supply_changed(&bci->ac);
	power_supply_changed(&bci->usb);

out:
	return IRQ_HANDLED;
}

/*
 * TWL4030 BCI monitoring events
 */
static irqreturn_t twl4030_bci_interrupt(int irq, void *arg)
{
	struct twl4030_bci *bci = arg;
	u8 irqs1, irqs2;
	int ret;

	ret = twl_i2c_read_u8(TWL4030_MODULE_INTERRUPTS, &irqs1,
			      TWL4030_INTERRUPTS_BCIISR1A);
	if (ret < 0)
		return IRQ_HANDLED;

	ret = twl_i2c_read_u8(TWL4030_MODULE_INTERRUPTS, &irqs2,
			      TWL4030_INTERRUPTS_BCIISR2A);
	if (ret < 0)
		return IRQ_HANDLED;

	dev_dbg(bci->dev, "BCI irq %02x %02x\n", irqs2, irqs1);

	if (irqs1 & (TWL4030_ICHGLOW | TWL4030_ICHGEOC)) {
		/* charger state change, inform the core */
		power_supply_changed(&bci->ac);
		power_supply_changed(&bci->usb);
	}

	/* various monitoring events, for now we just log them here */
	if (irqs1 & (TWL4030_TBATOR2 | TWL4030_TBATOR1) &&
			__ratelimit(&bci->ratelimit))
		dev_warn(bci->dev, "battery temperature out of range\n");

	if (irqs1 & TWL4030_BATSTS && __ratelimit(&bci->ratelimit))
		dev_crit(bci->dev, "battery disconnected\n");

	if (irqs2 & TWL4030_VBATOV && __ratelimit(&bci->ratelimit))
		dev_crit(bci->dev, "VBAT overvoltage\n");

	if (irqs2 & TWL4030_VBUSOV && __ratelimit(&bci->ratelimit))
		dev_crit(bci->dev, "VBUS overvoltage\n");

	if (irqs2 & TWL4030_ACCHGOV && __ratelimit(&bci->ratelimit))
		dev_crit(bci->dev, "Ac charger overvoltage\n");

#if 0
	/* ack the interrupts */
	twl_i2c_write_u8(TWL4030_MODULE_INTERRUPTS, irqs1,
			 TWL4030_INTERRUPTS_BCIISR1A);
	twl_i2c_write_u8(TWL4030_MODULE_INTERRUPTS, irqs2,
			 TWL4030_INTERRUPTS_BCIISR2A);
#endif

	return IRQ_HANDLED;
}

static void twl4030_bci_usb_work(struct work_struct *data)
{
	struct twl4030_bci *bci = container_of(data, struct twl4030_bci, work);

	switch (bci->event) {
	case USB_EVENT_VBUS:
	case USB_EVENT_CHARGER:
		twl4030_charger_enable_usb(bci, true);
		break;
	case USB_EVENT_NONE:
		twl4030_charger_enable_usb(bci, false);
		break;
	}
}

static int twl4030_bci_usb_ncb(struct notifier_block *nb, unsigned long val,
			       void *priv)
{
	struct twl4030_bci *bci = container_of(nb, struct twl4030_bci, otg_nb);

	dev_dbg(bci->dev, "OTG notify %lu\n", val);

	bci->event = val;
	schedule_work(&bci->work);

	return NOTIFY_OK;
}

/*
 * TI provided formulas:
 * CGAIN == 0: ICHG = (BCIICHG * 1.7) / (2^10 - 1) - 0.85
 * CGAIN == 1: ICHG = (BCIICHG * 3.4) / (2^10 - 1) - 1.7
 * Here we use integer approximation of:
 * CGAIN == 0: val * 1.6618 - 0.85
 * CGAIN == 1: (val * 1.6618 - 0.85) * 2
 */
static int twl4030_charger_get_current(void)
{
	int curr;
	int ret;
	u8 bcictl1;

	curr = twl4030bci_read_adc_val(TWL4030_BCIICHG);
	if (curr < 0)
		return curr;

	ret = twl4030_bci_read(TWL4030_BCICTL1, &bcictl1);
	if (ret)
		return ret;

	ret = (curr * 16618 - 850 * 10000) / 10;
	if (bcictl1 & TWL4030_CGAIN)
		ret *= 2;

	return ret;
}

static ssize_t twl4030_bci_ac_show_enable(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	u8 boot_bci;
	int ret;

	ret = twl_i2c_read_u8(TWL4030_MODULE_PM_MASTER, &boot_bci,
			      TWL4030_PM_MASTER_BOOT_BCI);
	if (ret)
		return ret;

	return sprintf(buf, "%d\n", (boot_bci & TWL4030_BCIAUTOAC) ? 1 : 0);
}

static ssize_t twl4030_bci_ac_store_enable(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct twl4030_bci *bci = container_of(psy, struct twl4030_bci, ac);
	unsigned long enable;
	int ret;

	ret = strict_strtoul(buf, 10, &enable);
	if (ret || enable > 1)
		return -EINVAL;

	bci->ac_charge_enable = enable;
	twl4030_charger_enable_ac(enable);

	return count;
}
static struct device_attribute dev_attr_enable_ac =
	__ATTR(enable, S_IRUGO | S_IWUSR, twl4030_bci_ac_show_enable,
	twl4030_bci_ac_store_enable);

static ssize_t twl4030_bci_usb_show_enable(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	u8 boot_bci;
	int ret;

	ret = twl_i2c_read_u8(TWL4030_MODULE_PM_MASTER, &boot_bci,
			      TWL4030_PM_MASTER_BOOT_BCI);
	if (ret)
		return ret;

	return sprintf(buf, "%d\n", (boot_bci & TWL4030_BCIAUTOUSB) ? 1 : 0);
}

static ssize_t twl4030_bci_usb_store_enable(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct twl4030_bci *bci = container_of(psy, struct twl4030_bci, usb);
	unsigned long enable;
	int ret;

	ret = strict_strtoul(buf, 10, &enable);
	if (ret || enable > 1)
		return -EINVAL;

	bci->usb_charge_enable = enable;
	twl4030_charger_enable_usb(bci, enable);

	return count;
}
static struct device_attribute dev_attr_enable_usb =
	__ATTR(enable, S_IRUGO | S_IWUSR, twl4030_bci_usb_show_enable,
	twl4030_bci_usb_store_enable);

static ssize_t show_charge_current(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int ret, val;
	u8 ctl;
	
	val = twl4030bci_read_adc_val(TWL4030_BCIIREF1);
	if (val < 0)
		return val;
	ret = twl4030_bci_read(TWL4030_BCICTL1, &ctl);
	if (ret < 0)
		return ret;

	val &= 0x1ff;
	if (ctl & TWL4030_CGAIN)
		val |= 0x200;

	return sprintf(buf, "%d\n", val);
}

static ssize_t store_charge_current(struct device *dev,
			struct device_attribute *attr, const char *buf,
			size_t count)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct twl4030_bci *bci = dev_get_drvdata(psy->dev->parent);
	unsigned long new_current;
	int ret;

	ret = strict_strtoul(buf, 10, &new_current);
	if (ret)
		return -EINVAL;

	ret = set_charge_current(bci, new_current);
	if (ret)
		return ret;

	if (psy->type == POWER_SUPPLY_TYPE_MAINS)
		bci->ac_current = new_current;
	else
		bci->usb_current = new_current;

	return count;
}
static DEVICE_ATTR(charge_current, S_IRUGO | S_IWUSR, show_charge_current,
	store_charge_current);

static struct attribute *bci_ac_attrs[] = {
	&dev_attr_enable_ac.attr,
	&dev_attr_charge_current.attr,
	NULL,
};

static struct attribute *bci_usb_attrs[] = {
	&dev_attr_enable_usb.attr,
	&dev_attr_charge_current.attr,
	NULL,
};
	
static const struct attribute_group bci_ac_attr_group = {
	.attrs = bci_ac_attrs,
};

static const struct attribute_group bci_usb_attr_group = {
	.attrs = bci_usb_attrs,
};

/*
 * Returns the main charge FSM state
 * Or < 0 on failure.
 */
static int twl4030bci_state(struct twl4030_bci *bci)
{
	int ret;
	u8 state;

	ret = twl4030_bci_read(TWL4030_BCIMSTATEC, &state);
	if (ret) {
		pr_err("twl4030_bci: error reading BCIMSTATEC\n");
		return ret;
	}

	dev_dbg(bci->dev, "state: %02x\n", state);

	return state;
}

static int twl4030_bci_state_to_status(int state)
{
	state &= TWL4030_MSTATEC_MASK;
	if (TWL4030_MSTATEC_QUICK1 <= state && state <= TWL4030_MSTATEC_QUICK7)
		return POWER_SUPPLY_STATUS_CHARGING;
	else if (TWL4030_MSTATEC_COMPLETE1 <= state &&
					state <= TWL4030_MSTATEC_COMPLETE4)
		return POWER_SUPPLY_STATUS_FULL;
	else
		return POWER_SUPPLY_STATUS_NOT_CHARGING;
}

static int twl4030_bci_get_property(struct power_supply *psy,
				    enum power_supply_property psp,
				    union power_supply_propval *val)
{
	struct twl4030_bci *bci = dev_get_drvdata(psy->dev->parent);
	int is_charging_any = 0;
	int is_charging = 0;
	int state;
	int ret;

	state = twl4030bci_state(bci);
	if (state < 0)
		return state;

	if (twl4030_bci_state_to_status(state) ==
	    POWER_SUPPLY_STATUS_CHARGING) {
		is_charging_any =
			state & (TWL4030_MSTATEC_USB | TWL4030_MSTATEC_AC);
		if (psy->type == POWER_SUPPLY_TYPE_USB)
			is_charging = state & TWL4030_MSTATEC_USB;
		else
			is_charging = state & TWL4030_MSTATEC_AC;
	}

	if (is_charging_any != bci->was_charging_any) {
		led_trigger_event(bci->charging_any_trig,
			is_charging_any ? LED_FULL : LED_OFF);
		bci->was_charging_any = is_charging_any;
	}

	if (is_charging && psy->type != bci->current_supply) {
		if (psy->type == POWER_SUPPLY_TYPE_USB)
			set_charge_current(bci, bci->usb_current);
		else
			set_charge_current(bci, bci->ac_current);
		bci->current_supply = psy->type;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (is_charging)
			val->intval = twl4030_bci_state_to_status(state);
		else
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		/* charging must be active for meaningful result */
		if (!is_charging)
			return -ENODATA;
		if (psy->type == POWER_SUPPLY_TYPE_USB) {
			ret = twl4030bci_read_adc_val(TWL4030_BCIVBUS);
			if (ret < 0)
				return ret;
			/* BCIVBUS uses ADCIN8, 7/1023 V/step */
			val->intval = ret * 6843;
		} else {
			ret = twl4030bci_read_adc_val(TWL4030_BCIVAC);
			if (ret < 0)
				return ret;
			/* BCIVAC uses ADCIN11, 10/1023 V/step */
			val->intval = ret * 9775;
		}
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (!is_charging)
			return -ENODATA;
		/* current measurement is shared between AC and USB */
		ret = twl4030_charger_get_current();
		if (ret < 0)
			return ret;
		val->intval = ret;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = is_charging &&
			twl4030_bci_state_to_status(state) !=
				POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static enum power_supply_property twl4030_charger_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
};

static int __init twl4030_bci_probe(struct platform_device *pdev)
{
	const struct twl4030_bci_platform_data *pdata = pdev->dev.platform_data;
	struct twl4030_bci *bci;
	int ret;
	u32 reg;

	if (pdata == NULL) {
		dev_err(&pdev->dev, "No platform data\n");
		return -EINVAL;
	}

	bci = kzalloc(sizeof(*bci), GFP_KERNEL);
	if (bci == NULL)
		return -ENOMEM;

	bci->dev = &pdev->dev;
	bci->irq_chg = platform_get_irq(pdev, 0);
	bci->irq_bci = platform_get_irq(pdev, 1);
	bci->ac_current = 860; /* ~1.2A */
	bci->usb_current = 330; /* ~560mA */
	bci->irq_had_charger = -1;
	bci->irq_check_count_time = jiffies;

	platform_set_drvdata(pdev, bci);

	ratelimit_state_init(&bci->ratelimit, HZ, 2);

	led_trigger_register_simple("twl4030_bci-charging",
		&bci->charging_any_trig);

	bci->ac.name = "twl4030_ac";
	bci->ac.type = POWER_SUPPLY_TYPE_MAINS;
	bci->ac.properties = twl4030_charger_props;
	bci->ac.num_properties = ARRAY_SIZE(twl4030_charger_props);
	bci->ac.get_property = twl4030_bci_get_property;
	bci->ac.supplied_to = pdata->supplied_to;
	bci->ac.num_supplicants = pdata->num_supplicants;

	ret = power_supply_register(&pdev->dev, &bci->ac);
	if (ret) {
		dev_err(&pdev->dev, "failed to register ac: %d\n", ret);
		goto fail_register_ac;
	}

	bci->usb.name = "twl4030_usb";
	bci->usb.type = POWER_SUPPLY_TYPE_USB;
	bci->usb.properties = twl4030_charger_props;
	bci->usb.num_properties = ARRAY_SIZE(twl4030_charger_props);
	bci->usb.get_property = twl4030_bci_get_property;
	bci->usb.supplied_to = pdata->supplied_to;
	bci->usb.num_supplicants = pdata->num_supplicants;

	bci->usb_reg = regulator_get(bci->dev, "bci3v1");
	if (IS_ERR(bci->usb_reg)) {
		dev_warn(&pdev->dev, "regulator get bci3v1 failed\n");
		bci->usb_reg = NULL;
	}

	ret = power_supply_register(&pdev->dev, &bci->usb);
	if (ret) {
		dev_err(&pdev->dev, "failed to register usb: %d\n", ret);
		goto fail_register_usb;
	}

	ret = request_threaded_irq(bci->irq_chg, NULL,
			twl4030_charger_interrupt, 0, pdev->name, bci);
	if (ret < 0) {
		dev_err(&pdev->dev, "could not request irq %d, status %d\n",
			bci->irq_chg, ret);
		goto fail_chg_irq;
	}

	ret = request_threaded_irq(bci->irq_bci, NULL,
			twl4030_bci_interrupt, 0, pdev->name, bci);
	if (ret < 0) {
		dev_err(&pdev->dev, "could not request irq %d, status %d\n",
			bci->irq_bci, ret);
		goto fail_bci_irq;
	}

	INIT_WORK(&bci->work, twl4030_bci_usb_work);

	bci->transceiver = otg_get_transceiver();
	if (bci->transceiver != NULL) {
		bci->otg_nb.notifier_call = twl4030_bci_usb_ncb;
		otg_register_notifier(bci->transceiver, &bci->otg_nb);
	}

	ret = sysfs_create_group(&bci->ac.dev->kobj, &bci_ac_attr_group);
	if (ret) {
		dev_err(&pdev->dev, "failed to create sysfs group: %d\n", ret);
		goto fail_sysfs1;
	}

	ret = sysfs_create_group(&bci->usb.dev->kobj, &bci_usb_attr_group);
	if (ret) {
		dev_err(&pdev->dev, "failed to create sysfs group: %d\n", ret);
		goto fail_sysfs2;
	}

	/* Enable interrupts now. */
	reg = ~(u32)(TWL4030_ICHGLOW | TWL4030_ICHGEOC | TWL4030_TBATOR2 |
		TWL4030_TBATOR1 | TWL4030_BATSTS);
	ret = twl_i2c_write_u8(TWL4030_MODULE_INTERRUPTS, reg,
			       TWL4030_INTERRUPTS_BCIIMR1A);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to unmask interrupts: %d\n", ret);
		goto fail_unmask_interrupts;
	}

	reg = ~(u32)(TWL4030_VBATOV | TWL4030_VBUSOV | TWL4030_ACCHGOV);
	ret = twl_i2c_write_u8(TWL4030_MODULE_INTERRUPTS, reg,
			       TWL4030_INTERRUPTS_BCIIMR2A);
	if (ret < 0)
		dev_warn(&pdev->dev, "failed to unmask interrupts: %d\n", ret);

	bci->ac_charge_enable = true;
	bci->usb_charge_enable = true;
	twl4030_charger_enable_ac(true);
	twl4030_charger_enable_usb(bci, true);

	return 0;

fail_unmask_interrupts:
	sysfs_remove_group(&bci->usb.dev->kobj, &bci_usb_attr_group);
fail_sysfs2:
	sysfs_remove_group(&bci->ac.dev->kobj, &bci_ac_attr_group);
fail_sysfs1:
	if (bci->transceiver != NULL) {
		otg_unregister_notifier(bci->transceiver, &bci->otg_nb);
		otg_put_transceiver(bci->transceiver);
	}
	free_irq(bci->irq_bci, bci);
fail_bci_irq:
	free_irq(bci->irq_chg, bci);
fail_chg_irq:
	power_supply_unregister(&bci->usb);
fail_register_usb:
	power_supply_unregister(&bci->ac);
fail_register_ac:
	led_trigger_unregister_simple(bci->charging_any_trig);
	platform_set_drvdata(pdev, NULL);
	kfree(bci);

	return ret;
}

static int __exit twl4030_bci_remove(struct platform_device *pdev)
{
	struct twl4030_bci *bci = platform_get_drvdata(pdev);

	sysfs_remove_group(&bci->usb.dev->kobj, &bci_usb_attr_group);
	sysfs_remove_group(&bci->ac.dev->kobj, &bci_ac_attr_group);

	twl4030_charger_enable_ac(false);
	twl4030_charger_enable_usb(bci, false);

	/* mask interrupts */
	twl_i2c_write_u8(TWL4030_MODULE_INTERRUPTS, 0xff,
			 TWL4030_INTERRUPTS_BCIIMR1A);
	twl_i2c_write_u8(TWL4030_MODULE_INTERRUPTS, 0xff,
			 TWL4030_INTERRUPTS_BCIIMR2A);

	if (bci->transceiver != NULL) {
		otg_unregister_notifier(bci->transceiver, &bci->otg_nb);
		otg_put_transceiver(bci->transceiver);
	}
	free_irq(bci->irq_bci, bci);
	free_irq(bci->irq_chg, bci);
	power_supply_unregister(&bci->usb);
	power_supply_unregister(&bci->ac);
	led_trigger_unregister_simple(bci->charging_any_trig);
	platform_set_drvdata(pdev, NULL);
	kfree(bci);

	return 0;
}

static struct platform_driver twl4030_bci_driver = {
	.driver	= {
		.name	= "twl4030_bci",
		.owner	= THIS_MODULE,
	},
	.remove	= __exit_p(twl4030_bci_remove),
};

static int __init twl4030_bci_init(void)
{
	return platform_driver_probe(&twl4030_bci_driver, twl4030_bci_probe);
}
module_init(twl4030_bci_init);

static void __exit twl4030_bci_exit(void)
{
	platform_driver_unregister(&twl4030_bci_driver);
}
module_exit(twl4030_bci_exit);

MODULE_AUTHOR("Gražvydas Ignotas");
MODULE_DESCRIPTION("TWL4030 Battery Charger Interface driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:twl4030_bci");
