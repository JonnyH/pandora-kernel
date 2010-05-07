/*
 * Backlight driver for TWL4030 PWM0 -> TPS61161 combo.
 * Based on pwm_bl.c
 *
 * Author: Gražvydas Ignotas <notasas@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/i2c/twl4030.h>
#include <linux/err.h>

#define TWL_PWM0_ON	0x00
#define TWL_PWM0_OFF	0x01

#define TWL_INTBR_GPBR1	0x0c
#define TWL_INTBR_PMBR1	0x0d

#define PWM0_CLK_ENABLE	1
#define PWM0_ENABLE	4

/* range accepted by hardware */
#define MIN_VALUE 9
#define MAX_VALUE 63
#define MAX_USER_VALUE (MAX_VALUE - MIN_VALUE)

static int old_brightness;

static void pwm0_disable(void)
{
	u8 r;

	/* first disable PWM0 output, then clock */
	twl4030_i2c_read_u8(TWL4030_MODULE_INTBR, &r, TWL_INTBR_GPBR1);
	r &= ~PWM0_ENABLE;
	twl4030_i2c_write_u8(TWL4030_MODULE_INTBR, r, TWL_INTBR_GPBR1);
	r &= ~PWM0_CLK_ENABLE;
	twl4030_i2c_write_u8(TWL4030_MODULE_INTBR, r, TWL_INTBR_GPBR1);
}

static int pwm0_backlight_update_status(struct backlight_device *bl)
{
	int brightness = bl->props.brightness;
	u8 r;

	if (bl->props.power != FB_BLANK_UNBLANK)
		brightness = 0;

	/* ignore fb blank for now
	if (bl->props.fb_blank != FB_BLANK_UNBLANK)
		brightness = 0;
	*/

	if ((unsigned int)brightness > MAX_USER_VALUE)
		brightness = MAX_USER_VALUE;

	if (brightness == 0) {
		if (old_brightness != 0)
			pwm0_disable();

		goto done;
	}

	if (old_brightness == 0) {
		/* set PWM duty cycle to max. TPS61161 seems to use this
		 * to calibrate it's PWM sensitivity when it starts. */
		twl4030_i2c_write_u8(TWL4030_MODULE_PWM0, MAX_VALUE,
					TWL_PWM0_OFF);

		/* first enable clock, then PWM0 out */
		twl4030_i2c_read_u8(TWL4030_MODULE_INTBR, &r, TWL_INTBR_GPBR1);
		r &= ~PWM0_ENABLE;
		r |= PWM0_CLK_ENABLE;
		twl4030_i2c_write_u8(TWL4030_MODULE_INTBR, r, TWL_INTBR_GPBR1);
		r |= PWM0_ENABLE;
		twl4030_i2c_write_u8(TWL4030_MODULE_INTBR, r, TWL_INTBR_GPBR1);

		/* TI made it very easy to enable digital control, so easy that
		 * it often triggers unintentionally and disabes PWM control,
		 * so wait until 1 wire mode detection window ends. */
		udelay(2000);
	}

	twl4030_i2c_write_u8(TWL4030_MODULE_PWM0, MIN_VALUE + brightness,
				TWL_PWM0_OFF);

done:
	old_brightness = brightness;
	return 0;
}

static int pwm0_backlight_get_brightness(struct backlight_device *bl)
{
	return bl->props.brightness;
}

static struct backlight_ops pwm0_backlight_ops = {
	.update_status	= pwm0_backlight_update_status,
	.get_brightness	= pwm0_backlight_get_brightness,
};

static int pwm0_backlight_probe(struct platform_device *pdev)
{
	struct backlight_device *bl;
	u8 r;

	bl = backlight_device_register(pdev->name, &pdev->dev,
			NULL, &pwm0_backlight_ops);
	if (IS_ERR(bl)) {
		dev_err(&pdev->dev, "failed to register backlight\n");
		return PTR_ERR(bl);
	}

	/* 64 cycle period, ON position 0 */
	twl4030_i2c_write_u8(TWL4030_MODULE_PWM0, 0x80, TWL_PWM0_ON);

	bl->props.max_brightness = MAX_USER_VALUE;
	bl->props.brightness = MAX_USER_VALUE;
	backlight_update_status(bl);

	/* enable PWM function in pin mux (i2c addr 0x49 0x92) */
	twl4030_i2c_read_u8(TWL4030_MODULE_INTBR, &r, TWL_INTBR_PMBR1);
	r &= ~0x0c;
	r |= 0x04;
	twl4030_i2c_write_u8(TWL4030_MODULE_INTBR, r, TWL_INTBR_PMBR1);

	return 0;
}

static int pwm0_backlight_remove(struct platform_device *pdev)
{
	struct backlight_device *bl = platform_get_drvdata(pdev);
	backlight_device_unregister(bl);
	return 0;
}

#ifdef CONFIG_PM
static int pwm0_backlight_suspend(struct platform_device *pdev,
				 pm_message_t state)
{
	pwm0_disable();
	old_brightness = 0;

	return 0;
}

static int pwm0_backlight_resume(struct platform_device *pdev)
{
	struct backlight_device *bl = platform_get_drvdata(pdev);

	backlight_update_status(bl);
	return 0;
}
#else
#define pwm0_backlight_suspend	NULL
#define pwm0_backlight_resume	NULL
#endif

static struct platform_driver pwm0_backlight_driver = {
	.driver		= {
		.name	= "twl4030-pwm0-bl",
		.owner	= THIS_MODULE,
	},
	.probe		= pwm0_backlight_probe,
	.remove		= pwm0_backlight_remove,
	.suspend	= pwm0_backlight_suspend,
	.resume		= pwm0_backlight_resume,
};

static int __init pwm0_backlight_init(void)
{
	return platform_driver_register(&pwm0_backlight_driver);
}
module_init(pwm0_backlight_init);

static void __exit pwm0_backlight_exit(void)
{
	platform_driver_unregister(&pwm0_backlight_driver);
}
module_exit(pwm0_backlight_exit);

MODULE_DESCRIPTION("TWL4030 PWM0 Backlight Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:twl4030-pwm0-bl");

