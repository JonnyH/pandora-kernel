/*
 * A driver for GPIO controlled backlights.
 * Based on pwm_bl.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/gpio.h>

static int gpio_backlight_update_status(struct backlight_device *bl)
{
	int gpio = (int)dev_get_drvdata(&bl->dev);
	int brightness = bl->props.brightness;

	if (bl->props.power != FB_BLANK_UNBLANK)
		brightness = 0;

	/* ignore fb blank for now
	if (bl->props.fb_blank != FB_BLANK_UNBLANK)
		brightness = 0;
	*/

	gpio_set_value(gpio, brightness);

	return 0;
}

static int gpio_backlight_get_brightness(struct backlight_device *bl)
{
	return bl->props.brightness;
}

static struct backlight_ops gpio_backlight_ops = {
	.update_status	= gpio_backlight_update_status,
	.get_brightness	= gpio_backlight_get_brightness,
};

static int gpio_backlight_probe(struct platform_device *pdev)
{
	struct backlight_device *bl;
	int gpio, ret;

	gpio = (int)pdev->dev.platform_data;

	bl = backlight_device_register(pdev->name, &pdev->dev,
			(void *)gpio, &gpio_backlight_ops);
	if (IS_ERR(bl)) {
		dev_err(&pdev->dev, "failed to register backlight\n");
		return PTR_ERR(bl);
	}

	ret = gpio_request(gpio, "backlight");
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request GPIO %d\n", gpio);
		goto err_bl;
	}

	ret = gpio_direction_output(gpio, 1);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to set GPIO %d direction\n", gpio);
		goto err_gpio;
	}

	bl->props.max_brightness = 1;
	bl->props.brightness = 1;
	backlight_update_status(bl);

	return 0;

err_gpio:
	gpio_free(gpio);
err_bl:
	backlight_device_unregister(bl);
	return ret;
}

static int gpio_backlight_remove(struct platform_device *pdev)
{
	struct backlight_device *bl = platform_get_drvdata(pdev);
	int gpio = (int)dev_get_drvdata(&bl->dev);

	gpio_free(gpio);
	backlight_device_unregister(bl);
	return 0;
}

#ifdef CONFIG_PM
static int gpio_backlight_suspend(struct platform_device *pdev,
				 pm_message_t state)
{
	struct backlight_device *bl = platform_get_drvdata(pdev);
	int gpio = (int)dev_get_drvdata(&bl->dev);

	gpio_set_value(gpio, 0);
	return 0;
}

static int gpio_backlight_resume(struct platform_device *pdev)
{
	struct backlight_device *bl = platform_get_drvdata(pdev);

	backlight_update_status(bl);
	return 0;
}
#else
#define gpio_backlight_suspend	NULL
#define gpio_backlight_resume	NULL
#endif

static struct platform_driver gpio_backlight_driver = {
	.driver		= {
		.name	= "gpio-backlight",
		.owner	= THIS_MODULE,
	},
	.probe		= gpio_backlight_probe,
	.remove		= gpio_backlight_remove,
	.suspend	= gpio_backlight_suspend,
	.resume		= gpio_backlight_resume,
};

static int __init gpio_backlight_init(void)
{
	return platform_driver_register(&gpio_backlight_driver);
}
module_init(gpio_backlight_init);

static void __exit gpio_backlight_exit(void)
{
	platform_driver_unregister(&gpio_backlight_driver);
}
module_exit(gpio_backlight_exit);

MODULE_DESCRIPTION("GPIO based Backlight Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:gpio-backlight");

