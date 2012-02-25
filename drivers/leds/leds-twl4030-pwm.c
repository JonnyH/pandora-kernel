/*
 * TWL4030 PWM controlled LED driver (LEDA, LEDB, PWM0, PWM1)
 *
 * Author: Gražvydas Ignotas <notasas@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/leds_pwm.h>
#include <linux/i2c/twl.h>
#include <linux/slab.h>

#define TWL_INTBR_GPBR1		0x0c
#define TWL_INTBR_PMBR1		0x0d

#define TWL4030_PWMx_PWMxON	0x00
#define TWL4030_PWMx_PWMxOFF	0x01
#define TWL4030_LED_LEDEN	0x00

#define GPBR1_PWM0_CLK_ENABLE	BIT(0)
#define GPBR1_PWM1_CLK_ENABLE	BIT(1)
#define GPBR1_PWM0_ENABLE	BIT(2)
#define GPBR1_PWM1_ENABLE	BIT(3)

/* LEDEN bits */
#define LEDEN_LEDAON		BIT(0)
#define LEDEN_LEDBON		BIT(1)
#define LEDEN_LEDAPWM		BIT(4)
#define LEDEN_LEDBPWM		BIT(5)

#define LED_UNKNOWN		-1

enum twl4030_led {
	TWL4030_LEDA = 0,
	TWL4030_LEDB,
	TWL4030_PWM0,
	TWL4030_PWM1,
};

struct twl4030_pwmled {
	struct led_classdev cdev;
	void (*enable)(enum twl4030_led led, bool enable);
	enum led_brightness new_brightness;
	enum led_brightness old_brightness;
	enum twl4030_led id;
	int module;
	struct work_struct work;
};

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

static void twl4030_enable_ledab(enum twl4030_led led, bool enable)
{
	u8 bits;

	if (led == TWL4030_LEDA)
		bits = LEDEN_LEDAON | LEDEN_LEDAPWM;
	else
		bits = LEDEN_LEDBON | LEDEN_LEDBPWM;

	if (enable)
		twl4030_clear_set(TWL4030_MODULE_LED, 0, bits, TWL4030_LED_LEDEN);
	else
		twl4030_clear_set(TWL4030_MODULE_LED, bits, 0, TWL4030_LED_LEDEN);
}

static void twl4030_enable_pwm01(enum twl4030_led led, bool enable)
{
	u8 r, enbit, clkbit;

	if (led == TWL4030_PWM0) {
		enbit = GPBR1_PWM0_ENABLE;
		clkbit = GPBR1_PWM0_CLK_ENABLE;
	} else {
		enbit = GPBR1_PWM1_ENABLE;
		clkbit = GPBR1_PWM1_CLK_ENABLE;
	}

	twl_i2c_read_u8(TWL4030_MODULE_INTBR, &r, TWL_INTBR_GPBR1);

	if (enable) {
		/* first enable clock, then PWM out */
		r &= ~enbit;
		r |= clkbit;
		twl_i2c_write_u8(TWL4030_MODULE_INTBR, r, TWL_INTBR_GPBR1);
		r |= enbit;
	} else {
		/* first disable PWM output, then clock */
		r &= ~enbit;
		twl_i2c_write_u8(TWL4030_MODULE_INTBR, r, TWL_INTBR_GPBR1);
		r &= ~clkbit;
	}

	twl_i2c_write_u8(TWL4030_MODULE_INTBR, r, TWL_INTBR_GPBR1);
}

static void twl4030_pwmled_work(struct work_struct *work)
{
	struct twl4030_pwmled *led;
	int val;

	led = container_of(work, struct twl4030_pwmled, work);

	if (led->new_brightness == LED_OFF) {
		if (led->old_brightness != LED_OFF)
			led->enable(led->id, 0);
		goto out;
	}

	val = led->new_brightness * 0x7f / LED_FULL;
	/* avoid 0: on = off = 0 means full brightness */
	if (val == 0)
		val = 1;

	twl_i2c_write_u8(led->module, val, TWL4030_PWMx_PWMxOFF);

	if (led->old_brightness == LED_OFF || led->old_brightness == LED_UNKNOWN)
		led->enable(led->id, 1);

out:
	led->old_brightness = led->new_brightness;
}

static void twl4030_pwmled_brightness(struct led_classdev *cdev,
		enum led_brightness b)
{
	struct twl4030_pwmled *led;

	led = container_of(cdev, struct twl4030_pwmled, cdev);
	led->new_brightness = b;
	schedule_work(&led->work);
}

static int __devinit twl4030_pwmled_probe(struct platform_device *pdev)
{
	const struct led_pwm_platform_data *pdata = pdev->dev.platform_data;
	struct twl4030_pwmled *led, *leds;
	int ret;
	int i;

	pdata = pdev->dev.platform_data;
	if (!pdata || pdata->num_leds < 1 || pdata->num_leds > 4)
		return -ENODEV;

	leds = kcalloc(pdata->num_leds, sizeof(*leds), GFP_KERNEL);
	if (!leds)
		return -ENOMEM;

	for (i = 0; i < pdata->num_leds; i++) {
		led = &leds[i];
		led->cdev.name = pdata->leds[i].name;
		led->cdev.brightness = LED_OFF;
		led->cdev.brightness_set = twl4030_pwmled_brightness;
		led->cdev.default_trigger = pdata->leds[i].default_trigger;
		led->id = pdata->leds[i].pwm_id;
		led->old_brightness = LED_UNKNOWN;

		switch (pdata->leds[i].pwm_id) {
		case TWL4030_LEDA:
			led->module = TWL4030_MODULE_PWMA;
			led->enable = twl4030_enable_ledab;
			break;
		case TWL4030_LEDB:
			led->module = TWL4030_MODULE_PWMB;
			led->enable = twl4030_enable_ledab;
			break;
		case TWL4030_PWM0:
			led->module = TWL4030_MODULE_PWM0;
			led->enable = twl4030_enable_pwm01;
			/* enable PWM0 in pin mux */
			twl4030_clear_set(TWL4030_MODULE_INTBR,
				0x0c, 0x04, TWL_INTBR_PMBR1);
			break;
		case TWL4030_PWM1:
			led->module = TWL4030_MODULE_PWM1;
			led->enable = twl4030_enable_pwm01;
			twl4030_clear_set(TWL4030_MODULE_INTBR,
				0, 0x30, TWL_INTBR_PMBR1);
			break;
		default:
			dev_err(&pdev->dev, "invalid pwm_id: %d\n",
				pdata->leds->pwm_id);
			ret = -ENODEV;
			goto err;
		}
		INIT_WORK(&led->work, twl4030_pwmled_work);

		twl_i2c_write_u8(led->module, 0, TWL4030_PWMx_PWMxON);

		/* Hand it over to the LED framework */
		ret = led_classdev_register(&pdev->dev, &led->cdev);
		if (ret < 0)
			goto err;
	}

	platform_set_drvdata(pdev, leds);
	return 0;

err:
	for (--i; i >= 0; i--)
		led_classdev_unregister(&leds[i].cdev);
	kfree(leds);

	return ret;
}

static int __devexit twl4030_pwmled_remove(struct platform_device *pdev)
{
	const struct led_pwm_platform_data *pdata = pdev->dev.platform_data;
	struct twl4030_pwmled *leds;
	int i;

	leds = platform_get_drvdata(pdev);

	for (i = 0; i < pdata->num_leds; i++) {
		led_classdev_unregister(&leds[i].cdev);
		cancel_work_sync(&leds[i].work);
	}

	kfree(leds);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static struct platform_driver twl4030_pwmled_driver = {
	.driver = {
		.name	= "leds-twl4030-pwm",
		.owner	= THIS_MODULE,
	},
	.probe	= twl4030_pwmled_probe,
	.remove = __devexit_p(twl4030_pwmled_remove),
};

static int __init twl4030_pwm_init(void)
{
	return platform_driver_register(&twl4030_pwmled_driver);
}
module_init(twl4030_pwm_init);

static void __exit twl4030_pwm_exit(void)
{
	platform_driver_unregister(&twl4030_pwmled_driver);
}
module_exit(twl4030_pwm_exit);

MODULE_AUTHOR("Gražvydas Ignotas");
MODULE_DESCRIPTION("Driver for TWL4030 PWM controlled LEDs");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:leds-twl4030-pwm");
