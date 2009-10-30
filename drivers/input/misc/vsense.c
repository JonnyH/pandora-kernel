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
#include <linux/uaccess.h>
#include <linux/ctype.h>
#include <linux/proc_fs.h>
#include <linux/i2c/vsense.h>
#include <linux/gpio.h>

#define VSENSE_MODE_ABS		0
#define VSENSE_MODE_MOUSE	1
#define VSENSE_MODE_SCROLL	2

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
	int mode;
	int scroll_counter;
	char dev_name[12];
};

static void vsense_work(struct work_struct *work)
{
	struct vsense_drvdata *ddata;
	int ax = 0, ay = 0, rx = 0, ry = 0;
	signed char buff[4];
	int ret;

	ddata = container_of(work, struct vsense_drvdata, work.work);

	if (unlikely(gpio_get_value(ddata->irq_gpio)))
		goto dosync;

	ret = i2c_master_recv(ddata->client, buff, sizeof(buff));
	if (unlikely(ret != sizeof(buff))) {
		dev_err(&ddata->client->dev, "read failed with %i\n", ret);
		goto dosync;
	}

	rx = (signed int)buff[0];
	ry = (signed int)buff[1];
	ax = (signed int)buff[2];
	ay = (signed int)buff[3];

	schedule_delayed_work(&ddata->work, msecs_to_jiffies(30));

dosync:
	switch (ddata->mode) {
	case VSENSE_MODE_MOUSE:
		input_report_rel(ddata->input, REL_X, rx);
		input_report_rel(ddata->input, REL_Y, -ry);
		break;
	case VSENSE_MODE_SCROLL:
		if (ddata->scroll_counter++ % 16 != 0)
			return;
		if (ax < 0)
			ax = ax < -8 ? ax / 8 : -1;
		else if (ax > 0)
			ax = ax >  8 ? ax / 8 :  1;
		if (ay < 0)
			ay = ay < -8 ? ay / 8 : -1;
		else if (ay > 0)
			ay = ay >  8 ? ay / 8 :  1;
		input_report_rel(ddata->input, REL_HWHEEL, ax);
		input_report_rel(ddata->input, REL_WHEEL, -ay);
		break;
	default:
		input_report_abs(ddata->input, ABS_X, ax * 8);
		input_report_abs(ddata->input, ABS_Y, ay * 8);
		break;
	}
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

static int vsense_input_register(struct vsense_drvdata *ddata, int mode)
{
	struct input_dev *input;
	int ret;

	input = input_allocate_device();
	if (input == NULL)
		return -ENOMEM;

	if (mode != VSENSE_MODE_ABS) {
		/* pretend to be a mouse */
		input_set_capability(input, EV_REL, REL_X);
		input_set_capability(input, EV_REL, REL_Y);
		input_set_capability(input, EV_REL, REL_WHEEL);
		input_set_capability(input, EV_REL, REL_HWHEEL);
		/* add fake buttons to fool X that this is a mouse */
		input_set_capability(input, EV_KEY, BTN_LEFT);
		input_set_capability(input, EV_KEY, BTN_RIGHT);
	} else {
		input->evbit[BIT_WORD(EV_ABS)] = BIT_MASK(EV_ABS);
		input_set_abs_params(input, ABS_X, -256, 256, 0, 0);
		input_set_abs_params(input, ABS_Y, -256, 256, 0, 0);
	}

	input->name = ddata->dev_name;
	input->dev.parent = &ddata->client->dev;

	input->id.bustype = BUS_I2C;
	input->id.version = 0x0091;

	input->open = vsense_open;
	input->close = vsense_close;

	ddata->input = input;
	input_set_drvdata(input, ddata);

	ret = input_register_device(input);
	if (ret) {
		dev_err(&ddata->client->dev, "failed to register input device,"
			" error %d\n", ret);
		input_free_device(input);
		return ret;
	}

	ddata->mode = mode;
	return 0;
}

static void vsense_input_unregister(struct vsense_drvdata *ddata)
{
	cancel_delayed_work_sync(&ddata->work);
	input_unregister_device(ddata->input);
}

static int vsense_proc_read(char *page, char **start, off_t off, int count,
		int *eof, void *data)
{
	struct vsense_drvdata *ddata = data;
	char *p = page;
	int len;

	switch (ddata->mode) {
	case VSENSE_MODE_MOUSE:
		len = sprintf(p, "mouse\n");
		break;
	case VSENSE_MODE_SCROLL:
		len = sprintf(p, "scroll\n");
		break;
	default:
		len = sprintf(p, "absolute\n");
		break;
	}

	*eof = 1;
	return len + 1;
}

static int vsense_proc_write(struct file *file, const char __user *buffer,
		unsigned long count, void *data)
{
	struct vsense_drvdata *ddata = data;
	int mode = ddata->mode;
	char buff[32], *p;
	int ret;

	count = strncpy_from_user(buff, buffer,
			count < sizeof(buff) ? count : sizeof(buff) - 1);
	buff[count] = 0;

	p = buff + strlen(buff) - 1;
	while (p > buff && isspace(*p))
		p--;
	p[1] = 0;

	if (strcasecmp(buff, "mouse") == 0)
		mode = VSENSE_MODE_MOUSE;
	else if (strcasecmp(buff, "scroll") == 0)
		mode = VSENSE_MODE_SCROLL;
	else if (strcasecmp(buff, "absolute") == 0)
		mode = VSENSE_MODE_ABS;
	else
		dev_err(&ddata->client->dev, "unknown mode: %s\n", buff);

	if (mode != ddata->mode) {
		disable_irq(ddata->client->irq);
		vsense_input_unregister(ddata);
		ret = vsense_input_register(ddata, mode);
		if (ret)
			dev_err(&ddata->client->dev, "failed to re-register "
				"input as %d, code %d\n", mode, ret);
		else
			enable_irq(ddata->client->irq);
	}

	return count;
}

static int vsense_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct vsense_platform_data *pdata = client->dev.platform_data;
	struct vsense_drvdata *ddata;
	struct proc_dir_entry *pret;
	char buff[32];
	int ret;

	if (pdata == NULL) {
		dev_err(&client->dev, "no platform data?\n");
		return -EINVAL;
	}

	if (i2c_check_functionality(client->adapter, I2C_FUNC_I2C) == 0) {
		dev_err(&client->dev, "can't talk I2C?\n");
		return -EIO;
	}

	ddata = kzalloc(sizeof(struct vsense_drvdata), GFP_KERNEL);
	if (ddata == NULL)
		return -ENOMEM;

	snprintf(ddata->dev_name, sizeof(ddata->dev_name),
		"vsense%02x", client->addr);

	ret = gpio_request(pdata->gpio_irq, client->name);
	if (ret < 0) {
		dev_err(&client->dev, "failed to request GPIO %d,"
			" error %d\n", pdata->gpio_irq, ret);
		goto fail0;
	}

	ret = gpio_direction_input(pdata->gpio_irq);
	if (ret < 0) {
		dev_err(&client->dev, "failed to configure input direction "
			"for GPIO %d, error %d\n", pdata->gpio_irq, ret);
		goto fail1;
	}

	ret = gpio_to_irq(pdata->gpio_irq);
	if (ret < 0) {
		dev_err(&client->dev, "unable to get irq number for GPIO %d, "
			"error %d\n", pdata->gpio_irq, ret);
		goto fail1;
	}
	client->irq = ret;

	INIT_DELAYED_WORK(&ddata->work, vsense_work);
	ddata->mode = VSENSE_MODE_ABS;
	ddata->client = client;
	ddata->reset_gpio = pdata->gpio_reset;
	ddata->irq_gpio = pdata->gpio_irq;
	i2c_set_clientdata(client, ddata);

	ret = vsense_input_register(ddata, ddata->mode);
	if (ret) {
		dev_err(&client->dev, "failed to register input device, "
			"error %d\n", ret);
		goto fail1;
	}

	ret = request_irq(client->irq, vsense_isr,
			IRQF_SAMPLE_RANDOM | IRQF_TRIGGER_FALLING,
			client->name, ddata);
	if (ret) {
		dev_err(&client->dev, "unable to claim irq %d, error %d\n",
			client->irq, ret);
		goto fail1;
	}

	dev_dbg(&client->dev, "probe %02x, gpio %i, irq %i, \"%s\"\n",
		client->addr, pdata->gpio_irq, client->irq, client->name);

	snprintf(buff, sizeof(buff), "pandora/vsense%02x", client->addr);
	pret = create_proc_entry(buff, S_IWUGO | S_IRUGO, NULL);
	if (pret == NULL) {
		proc_mkdir("pandora", NULL);
		pret = create_proc_entry(buff, S_IWUSR | S_IRUGO, NULL);
		if (pret == NULL)
			dev_err(&client->dev, "can't create proc file");
	}

	pret->data = ddata;
	pret->read_proc = vsense_proc_read;
	pret->write_proc = vsense_proc_write;

	return 0;

fail1:
	gpio_free(pdata->gpio_irq);
fail0:
	kfree(ddata);
	return ret;
}

static int __devexit vsense_remove(struct i2c_client *client)
{
	struct vsense_drvdata *ddata;
	char buff[32];

	dev_dbg(&client->dev, "remove\n");

	ddata = i2c_get_clientdata(client);

	snprintf(buff, sizeof(buff), "pandora/vsense%02x", client->addr);
	remove_proc_entry(buff, NULL);
	free_irq(client->irq, ddata);
	vsense_input_unregister(ddata);
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
