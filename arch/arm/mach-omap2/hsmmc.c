/*
 * linux/arch/arm/mach-omap2/board-sdp-hsmmc.c
 *
 * Copyright (C) 2007-2008 Texas Instruments
 * Copyright (C) 2008 Nokia Corporation
 * Author: Texas Instruments
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c/twl4030.h>

#include <mach/hardware.h>
#include <mach/mmc.h>
#include <mach/board.h>

#if defined(CONFIG_MMC_OMAP_HS) || defined(CONFIG_MMC_OMAP_HS_MODULE)

#define VMMC1_DEV_GRP		0x27
#define P1_DEV_GRP		0x20
#define VMMC1_DEDICATED		0x2A
#define VSEL_3V			0x02
#define VSEL_3V15		0x03
#define VSEL_18V		0x00
#define TWL_GPIO_IMR1A		0x1C
#define TWL_GPIO_ISR1A		0x19
#define LDO_CLR			0x00
#define VSEL_S2_CLR		0x40
#define GPIO_0_BIT_POS		(1 << 0)

#define OMAP2_CONTROL_DEVCONF0	0x48002274
#define OMAP2_CONTROL_DEVCONF1	0x490022E8

#define OMAP2_CONTROL_DEVCONF0_LBCLK	(1 << 24)
#define OMAP2_CONTROL_DEVCONF1_ACTOV	(1 << 31)

#define OMAP2_CONTROL_PBIAS_VMODE	(1 << 0)
#define OMAP2_CONTROL_PBIAS_PWRDNZ	(1 << 1)
#define OMAP2_CONTROL_PBIAS_SCTRL	(1 << 2)


static const int mmc1_cd_gpio = OMAP_MAX_GPIO_LINES;		/* HACK!! */

static int hsmmc_card_detect(int irq)
{
	return gpio_get_value_cansleep(mmc1_cd_gpio);
}

/*
 * MMC Slot Initialization.
 */
static int hsmmc_late_init(struct device *dev)
{
	int ret = 0;

	/*
	 * Configure TWL4030 GPIO parameters for MMC hotplug irq
	 */
	ret = gpio_request(mmc1_cd_gpio, "mmc0_cd");
	if (ret)
		goto err;

	ret = twl4030_set_gpio_debounce(0, true);
	if (ret)
		goto err;

	return ret;

err:
	dev_err(dev, "Failed to configure TWL4030 GPIO IRQ\n");
	return ret;
}

static void hsmmc_cleanup(struct device *dev)
{
	gpio_free(mmc1_cd_gpio);
}

#ifdef CONFIG_PM

/*
 * To mask and unmask MMC Card Detect Interrupt
 * mask : 1
 * unmask : 0
 */
static int mask_cd_interrupt(int mask)
{
	u8 reg = 0, ret = 0;

	ret = twl4030_i2c_read_u8(TWL4030_MODULE_GPIO, &reg, TWL_GPIO_IMR1A);
	if (ret)
		goto err;

	reg = (mask == 1) ? (reg | GPIO_0_BIT_POS) : (reg & ~GPIO_0_BIT_POS);

	ret = twl4030_i2c_write_u8(TWL4030_MODULE_GPIO, reg, TWL_GPIO_IMR1A);
	if (ret)
		goto err;

	ret = twl4030_i2c_read_u8(TWL4030_MODULE_GPIO, &reg, TWL_GPIO_ISR1A);
	if (ret)
		goto err;

	reg = (mask == 1) ? (reg | GPIO_0_BIT_POS) : (reg & ~GPIO_0_BIT_POS);

	ret = twl4030_i2c_write_u8(TWL4030_MODULE_GPIO, reg, TWL_GPIO_ISR1A);
	if (ret)
		goto err;

err:
	return ret;
}

static int hsmmc_suspend(struct device *dev, int slot)
{
	int ret = 0;

	disable_irq(TWL4030_GPIO_IRQ_NO(0));
	ret = mask_cd_interrupt(1);

	return ret;
}

static int hsmmc_resume(struct device *dev, int slot)
{
	int ret = 0;

	enable_irq(TWL4030_GPIO_IRQ_NO(0));
	ret = mask_cd_interrupt(0);

	return ret;
}

#endif

static int hsmmc_set_power(struct device *dev, int slot, int power_on,
				int vdd)
{
	u32 vdd_sel = 0, devconf = 0, reg = 0;
	int ret = 0;

	dev_dbg(dev, "power %s, vdd %i\n", power_on ? "on" : "off", vdd);

	/* REVISIT: Using address directly till the control.h defines
	 * are settled.
	 */
#if defined(CONFIG_ARCH_OMAP2430)
	#define OMAP2_CONTROL_PBIAS 0x490024A0
#else
	#define OMAP2_CONTROL_PBIAS 0x48002520
#endif

	if (power_on) {
		if (cpu_is_omap24xx())
			devconf = omap_readl(OMAP2_CONTROL_DEVCONF1);
		else
			devconf = omap_readl(OMAP2_CONTROL_DEVCONF0);

		switch (1 << vdd) {
		case MMC_VDD_33_34:
		case MMC_VDD_32_33:
			vdd_sel = VSEL_3V15;
			if (cpu_is_omap24xx())
				devconf |= OMAP2_CONTROL_DEVCONF1_ACTOV;
			break;
		case MMC_VDD_165_195:
			vdd_sel = VSEL_18V;
			if (cpu_is_omap24xx())
				devconf &= ~OMAP2_CONTROL_DEVCONF1_ACTOV;
		}

		if (cpu_is_omap24xx())
			omap_writel(devconf, OMAP2_CONTROL_DEVCONF1);
		else
			omap_writel(devconf | OMAP2_CONTROL_DEVCONF0_LBCLK,
				    OMAP2_CONTROL_DEVCONF0);

		reg = omap_readl(OMAP2_CONTROL_PBIAS);
		reg |= OMAP2_CONTROL_PBIAS_SCTRL;
		omap_writel(reg, OMAP2_CONTROL_PBIAS);

		reg = omap_readl(OMAP2_CONTROL_PBIAS);
		reg &= ~OMAP2_CONTROL_PBIAS_PWRDNZ;
		omap_writel(reg, OMAP2_CONTROL_PBIAS);

		ret = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
						P1_DEV_GRP, VMMC1_DEV_GRP);
		if (ret)
			goto err;

		ret = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
						vdd_sel, VMMC1_DEDICATED);
		if (ret)
			goto err;

		msleep(100);
		reg = omap_readl(OMAP2_CONTROL_PBIAS);
		reg |= (OMAP2_CONTROL_PBIAS_SCTRL |
			OMAP2_CONTROL_PBIAS_PWRDNZ);
		if (vdd_sel == VSEL_18V)
			reg &= ~OMAP2_CONTROL_PBIAS_VMODE;
		else
			reg |= OMAP2_CONTROL_PBIAS_VMODE;
		omap_writel(reg, OMAP2_CONTROL_PBIAS);

		return ret;

	} else {
		/* Power OFF */

		/* For MMC1, Toggle PBIAS before every power up sequence */
		reg = omap_readl(OMAP2_CONTROL_PBIAS);
		reg &= ~OMAP2_CONTROL_PBIAS_PWRDNZ;
		omap_writel(reg, OMAP2_CONTROL_PBIAS);

		ret = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
						LDO_CLR, VMMC1_DEV_GRP);
		if (ret)
			goto err;

		ret = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
						VSEL_S2_CLR, VMMC1_DEDICATED);
		if (ret)
			goto err;

		/* 100ms delay required for PBIAS configuration */
		msleep(100);
		reg = omap_readl(OMAP2_CONTROL_PBIAS);
		reg |= (OMAP2_CONTROL_PBIAS_VMODE |
			OMAP2_CONTROL_PBIAS_PWRDNZ |
			OMAP2_CONTROL_PBIAS_SCTRL);
		omap_writel(reg, OMAP2_CONTROL_PBIAS);
	}

	return 0;

err:
	return 1;
}

static struct omap_mmc_platform_data mmc1_data = {
	.nr_slots			= 1,
	.init				= hsmmc_late_init,
	.cleanup			= hsmmc_cleanup,
#ifdef CONFIG_PM
	.suspend			= hsmmc_suspend,
	.resume				= hsmmc_resume,
#endif
	.dma_mask			= 0xffffffff,
	.slots[0] = {
		.wire4			= 1,
		.set_power		= hsmmc_set_power,
		.ocr_mask		= MMC_VDD_32_33 | MMC_VDD_33_34 |
						MMC_VDD_165_195,
		.name			= "first slot",

		.card_detect_irq        = TWL4030_GPIO_IRQ_NO(0),
		.card_detect            = hsmmc_card_detect,
	},
};

/* ************************************************************************* */

#define VMMC2_DEV_GRP		0x2B
#define VMMC2_DEDICATED		0x2E

#define mmc2_cd_gpio (mmc1_cd_gpio + 1)

static int hsmmc2_card_detect(int irq)
{
	return gpio_get_value_cansleep(mmc2_cd_gpio);
}

static int hsmmc2_late_init(struct device *dev)
{
	int ret = 0;

	ret = gpio_request(mmc2_cd_gpio, "mmc1_cd");
	if (ret)
		goto err;

	ret = twl4030_set_gpio_debounce(1, true);
	if (ret)
		goto err;

	return ret;

err:
	dev_err(dev, "Failed to configure TWL4030 GPIO IRQ for MMC2\n");
	return ret;
}

static void hsmmc2_cleanup(struct device *dev)
{
	gpio_free(mmc2_cd_gpio);
}

static int hsmmc2_set_power(struct device *dev, int slot, int power_on,
				int vdd)
{
	u32 vdd_sel = 0, ret = 0;

	dev_dbg(dev, "power %s, vdd %i\n", power_on ? "on" : "off", vdd);

	if (power_on) {
		switch (1 << vdd) {
		case MMC_VDD_33_34:
		case MMC_VDD_32_33:
			vdd_sel = 0x0c;
			break;
		case MMC_VDD_165_195:
			vdd_sel = 0x05;
			break;
		default:
			dev_err(dev, "Bad vdd request %i for MMC2\n", vdd);
			goto err;
		}

		ret = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
						P1_DEV_GRP, VMMC2_DEV_GRP);
		if (ret)
			goto err;

		ret = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
						vdd_sel, VMMC2_DEDICATED);
		if (ret)
			goto err;

		msleep(100);
	} else {
		/* Power OFF */
		ret = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
						LDO_CLR, VMMC2_DEV_GRP);
		if (ret)
			goto err;

		ret = twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
						VSEL_S2_CLR, VMMC2_DEDICATED);
		if (ret)
			goto err;
	}

	return 0;

err:
	return ret;
}

static struct omap_mmc_platform_data mmc2_data = {
	.nr_slots			= 1,
	.init				= hsmmc2_late_init,
	.cleanup			= hsmmc2_cleanup,
	/* TODO: .suspend, .resume */
	.dma_mask			= 0xffffffff,
	.slots[0] = {
		.wire4			= 1,
		.set_power		= hsmmc2_set_power,
		.ocr_mask		= MMC_VDD_32_33 | MMC_VDD_33_34 |
						MMC_VDD_165_195,
		.name			= "second slot",

		.card_detect_irq        = TWL4030_GPIO_IRQ_NO(1),
		.card_detect            = hsmmc2_card_detect,
	},
};

/* ************************************************************************* */

static int hsmmc3_set_power(struct device *dev, int slot, int power_on,
		int vdd)
{
	/* nothing to do for MMC3 */
	return 0;
}

/*
 * Hack: Hardcoded WL1251 embedded data for Pandora
 * - passed up via a dirty hack to the MMC platform data.
 */

#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>

static struct sdio_embedded_func wifi_func = {
	.f_class	= SDIO_CLASS_WLAN,
	.f_maxblksize	= 512,
};

static struct embedded_sdio_data pandora_wifi_emb_data = {
	.cis	= {
		.vendor		= 0x104c,
		.device		= 0x9066,
		.blksize	= 512,
		.max_dtr	= 20000000,
	},
	.cccr	= {
		.multi_block	= 0,
		.low_speed	= 0,
		.wide_bus	= 1,
		.high_power	= 0,
		.high_speed	= 0,
	},
	.funcs	= &wifi_func,
	.num_funcs = 1,
};

static struct omap_mmc_platform_data mmc3_data = {
	.nr_slots                       = 1,
	.dma_mask                       = 0xffffffff,
	.embedded_sdio                  = &pandora_wifi_emb_data,
	.slots[0] = {
		.wire4                  = 1,
		.set_power              = hsmmc3_set_power,
		.ocr_mask               = MMC_VDD_165_195 | MMC_VDD_20_21,
		.name                   = "third slot",
	},
};

/* ************************************************************************* */

static struct omap_mmc_platform_data *hsmmc_data[OMAP34XX_NR_MMC];

void __init hsmmc_init(void)
{
	hsmmc_data[0] = &mmc1_data;
	hsmmc_data[1] = &mmc2_data;
	hsmmc_data[2] = &mmc3_data;
	omap2_init_mmc(hsmmc_data, OMAP34XX_NR_MMC);
}

#else

void __init hsmmc_init(void)
{

}

#endif
