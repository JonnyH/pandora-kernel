/*
 * Backlight driver for TWL4030 PWM0.
 * Based on pwm_bl.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/i2c/twl4030.h>
#include <linux/err.h>

#define TWL_PWM0_ON	0x00
#define TWL_PWM0_OFF	0x01

#define TWL_INTBR_GPBR1	0x0c
#define TWL_INTBR_PMBR1	0x0d

static int old_brightness;

static int pwm0_backlight_update_status(struct backlight_device *bl)
{
	int brightness = bl->props.brightness;

	if (bl->props.power != FB_BLANK_UNBLANK)
		brightness = 0;

	/* ignore fb blank for now
	if (bl->props.fb_blank != FB_BLANK_UNBLANK)
		brightness = 0;
	*/

	if ((unsigned)brightness > 57)
		brightness = 57;

	if (brightness == 0) {
		if (old_brightness != 0) {
			/* set OFF time to max,
			 * then disable PWM0 output and clock */
			twl4030_i2c_write_u8(TWL4030_MODULE_PWM0, 0x3f,
						TWL_PWM0_OFF);
			/* 0x49 0x91 */
			twl4030_i2c_write_u8(TWL4030_MODULE_INTBR, 0x01,
						TWL_INTBR_GPBR1);
			twl4030_i2c_write_u8(TWL4030_MODULE_INTBR, 0x00,
						TWL_INTBR_GPBR1);
		}

		goto done;
	}

	if (old_brightness == 0)
		/* turn PWM0 output and clock on */
		twl4030_i2c_write_u8(TWL4030_MODULE_INTBR, 0x05,
					TWL_INTBR_GPBR1);

	twl4030_i2c_write_u8(TWL4030_MODULE_PWM0, 6 + brightness,
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

	bl = backlight_device_register(pdev->name, &pdev->dev,
			NULL, &pwm0_backlight_ops);
	if (IS_ERR(bl)) {
		dev_err(&pdev->dev, "failed to register backlight\n");
		return PTR_ERR(bl);
	}

	twl4030_i2c_write_u8(TWL4030_MODULE_PWM0, 0x81, TWL_PWM0_ON);

	bl->props.max_brightness = 57;
	bl->props.brightness = 57;
	backlight_update_status(bl);

	/* enable PWM function in pin mux (i2c addr 0x49 0x92) */
	twl4030_i2c_write_u8(TWL4030_MODULE_INTBR, 0x04, TWL_INTBR_PMBR1);

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
	/* turn PWM0 off */
	twl4030_i2c_write_u8(TWL4030_MODULE_INTBR, 0x01, TWL_INTBR_GPBR1);
	twl4030_i2c_write_u8(TWL4030_MODULE_INTBR, 0x00, TWL_INTBR_GPBR1);
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

