/*
	vsense.c

	Written by Gražvydas Ignotas <notasas@gmail.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; version 2 of the License.
*/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/i2c/vsense.h>
#include <linux/gpio.h>

/* hack for Pandora: keep track of usage to prevent reset
 * while other nub is in use
 */
static int reference_count;

struct vsense_drvdata {
	struct input_dev *input;
	struct i2c_client *client;
	struct delayed_work work;
	int reset_gpio;
	int irq_gpio;
	char dev_name[12];
};

static void vsense_work(struct work_struct *work)
{
	struct vsense_drvdata *ddata;
	signed char buff[8];
	int ret, x = 0, y = 0;

	ddata = container_of(work, struct vsense_drvdata, work.work);

	if (unlikely(gpio_get_value(ddata->irq_gpio)))
		goto dosync;

	ret = i2c_master_recv(ddata->client, buff, 8);
	if (unlikely(ret != 8)) {
		dev_err(&ddata->client->dev, "read failed with %i\n", ret);
		goto dosync;
	}

	x = (signed int)buff[2] * 8;
	y = (signed int)buff[3] * 8;

	schedule_delayed_work(&ddata->work, msecs_to_jiffies(25));

dosync:
	input_report_abs(ddata->input, ABS_X, x);
	input_report_abs(ddata->input, ABS_Y, y);
	input_sync(ddata->input);
}

static irqreturn_t vsense_isr(int irq, void *dev_id)
{
	struct vsense_drvdata *ddata = dev_id;

	schedule_delayed_work(&ddata->work, 0);

	return IRQ_HANDLED;
}

static int vsense_reset(struct input_dev *dev, int val)
{
	struct vsense_drvdata *ddata;
	int ret;

	ddata = input_get_drvdata(dev);

	dev_dbg(&dev->dev, "vsense_reset: %i\n", val);

	ret = gpio_request(ddata->reset_gpio, "vsense reset");
	if (ret < 0) {
		dev_err(&dev->dev, "failed to request GPIO %d,"
				" error %d\n", ddata->reset_gpio, ret);
		return ret;
	}

	ret = gpio_direction_output(ddata->reset_gpio, val);
	if (ret < 0) {
		dev_err(&dev->dev, "failed to configure input direction "
			"for GPIO %d, error %d\n", ddata->reset_gpio, ret);
	}

	gpio_free(ddata->reset_gpio);
	return ret;
}

static int vsense_open(struct input_dev *dev)
{
	dev_dbg(&dev->dev, "vsense_open\n");

	if (reference_count++ == 0)
		vsense_reset(dev, 0);

	return 0;
}

static void vsense_close(struct input_dev *dev)
{
	dev_dbg(&dev->dev, "vsense_close\n");

	if (--reference_count == 0)
		vsense_reset(dev, 1);
	BUG_ON(reference_count < 0);
}

static int vsense_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct vsense_platform_data *pdata = client->dev.platform_data;
	struct vsense_drvdata *ddata;
	struct input_dev *input;
	int ret;

	if (pdata == NULL) {
		dev_err(&client->dev, "no platform data?\n");
		return -EINVAL;
	}

	if (i2c_check_functionality(client->adapter, I2C_FUNC_I2C) == 0) {
		dev_err(&client->dev, "can't talk I2C?\n");
		return -EIO;
	}

	input = input_allocate_device();
	if (input == NULL)
		return -ENOMEM;

	ddata = kzalloc(sizeof(struct vsense_drvdata), GFP_KERNEL);
	if (ddata == NULL) {
		ret = -ENOMEM;
		goto fail1;
	}

	snprintf(ddata->dev_name, sizeof(ddata->dev_name),
		"vsense%02x", client->addr);

	input->evbit[0] = BIT_MASK(EV_ABS);
	input_set_abs_params(input, ABS_X, -256, 256, 0, 0);
	input_set_abs_params(input, ABS_Y, -256, 256, 0, 0);

	input->name = ddata->dev_name;
	input->dev.parent = &client->dev;

	input->id.bustype = BUS_I2C;
	input->id.version = 0x0091;

	input->open = vsense_open;
	input->close = vsense_close;

	ret = gpio_request(pdata->gpio_irq, client->name);
	if (ret < 0) {
		dev_err(&client->dev, "failed to request GPIO %d,"
			" error %d\n", pdata->gpio_irq, ret);
		goto fail2;
	}

	ret = gpio_direction_input(pdata->gpio_irq);
	if (ret < 0) {
		dev_err(&client->dev, "failed to configure input direction "
			"for GPIO %d, error %d\n", pdata->gpio_irq, ret);
		goto fail3;
	}

	ret = gpio_to_irq(pdata->gpio_irq);
	if (ret < 0) {
		dev_err(&client->dev, "unable to get irq number for GPIO %d, "
			"error %d\n", pdata->gpio_irq, ret);
		goto fail3;
	}
	client->irq = ret;

	ret = request_irq(client->irq, vsense_isr,
			IRQF_SAMPLE_RANDOM | IRQF_TRIGGER_FALLING,
			client->name, ddata);
	if (ret) {
		dev_err(&client->dev, "unable to claim irq %d, error %d\n",
			client->irq, ret);
		goto fail3;
	}

	INIT_DELAYED_WORK(&ddata->work, vsense_work);

	ret = input_register_device(input);
	if (ret) {
		dev_err(&client->dev, "failed to register input device, "
			"error %d\n", ret);
		goto fail4;
	}

	ddata->input = input;
	ddata->client = client;
	ddata->reset_gpio = pdata->gpio_reset;
	ddata->irq_gpio = pdata->gpio_irq;
	i2c_set_clientdata(client, ddata);
	input_set_drvdata(input, ddata);

	dev_dbg(&client->dev, "probe %02x, gpio %i, irq %i, \"%s\"\n",
		client->addr, pdata->gpio_irq, client->irq, client->name);

	return 0;

fail4:
	free_irq(client->irq, ddata);
fail3:
	gpio_free(pdata->gpio_irq);
fail2:
	kfree(ddata);
fail1:
	input_free_device(input);
	return ret;
}

static int __devexit vsense_remove(struct i2c_client *client)
{
	struct vsense_drvdata *ddata;

	dev_dbg(&client->dev, "remove\n");

	ddata = i2c_get_clientdata(client);

	free_irq(client->irq, ddata);
	cancel_delayed_work_sync(&ddata->work);
	input_unregister_device(ddata->input);
	gpio_free(ddata->irq_gpio);
	kfree(ddata);

	return 0;
}

static const struct i2c_device_id vsense_id[] = {
	{ "vsense", 0 },
	{ }
};

static struct i2c_driver vsense_driver = {
	.driver = {
		.name	= "vsense",
	},
	.probe		= vsense_probe,
	.remove		= __devexit_p(vsense_remove),
	.id_table	= vsense_id,
};

static int __init vsense_init(void)
{
	return i2c_add_driver(&vsense_driver);
}

static void __exit vsense_exit(void)
{
	i2c_del_driver(&vsense_driver);
}


MODULE_AUTHOR("Grazvydas Ignotas");
MODULE_DESCRIPTION("VSense navigation device driver");
MODULE_LICENSE("GPL");

module_init(vsense_init);
module_exit(vsense_exit);
