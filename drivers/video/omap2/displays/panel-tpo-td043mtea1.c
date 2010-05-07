/*
 * LCD panel driver for TPO TD043MTEA1
 *
 * Author: Gražvydas Ignotas <notasas@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>

#include <mach/display.h>

#define TPO_R02_MODE(x)		((x) & 7)
#define TPO_R02_NCLK_RISING	(1 << 3)
#define TPO_R02_HSYNC_HIGH	(1 << 4)
#define TPO_R02_VSYNC_HIGH	(1 << 5)

#define TPO_R03_NSTANDBY	(1 << 0)
#define TPO_R03_EN_CP_CLK	(1 << 1)
#define TPO_R03_EN_VGL_PUMP	(1 << 2)
#define TPO_R03_EN_PWM		(1 << 3)
#define TPO_R03_DRIVING_CAP_100	(1 << 4)
#define TPO_R03_EN_PRE_CHARGE	(1 << 6)
#define TPO_R03_SOFTWARE_CTL	(1 << 7)

#define TPO_R04_NFLIP_H		(1 << 0)
#define TPO_R04_NFLIP_V		(1 << 1)
#define TPO_R04_CP_CLK_FREQ_1H	(1 << 2)
#define TPO_R04_VGL_FREQ_1H	(1 << 4)

#define TPO_R03_VAL_NORMAL (TPO_R03_NSTANDBY | TPO_R03_EN_CP_CLK | \
			TPO_R03_EN_VGL_PUMP |  TPO_R03_EN_PWM | \
			TPO_R03_DRIVING_CAP_100 | TPO_R03_EN_PRE_CHARGE | \
			TPO_R03_SOFTWARE_CTL)

#define TPO_R03_VAL_STANDBY (TPO_R03_EN_PWM | TPO_R03_DRIVING_CAP_100 | \
			TPO_R03_EN_PRE_CHARGE | TPO_R03_SOFTWARE_CTL)

static const u16 tpo_td043_def_gamma[12] = {
	105, 315, 381, 431, 490, 537, 579, 686, 780, 837, 880, 1023
};

struct tpo_td043_device {
	struct spi_device *spi;
	int mirror;
	int mode;
	u16 gamma[12];
};

static void tpo_td043_write(struct spi_device *spi, u8 addr, u8 data)
{
	struct spi_message	m;
	struct spi_transfer	xfer;
	u16			w;
	int			r;

	spi_message_init(&m);

	memset(&xfer, 0, sizeof(xfer));

	w = ((u16)addr << 10) | (1 << 8) | data;
	xfer.tx_buf = &w;
	xfer.bits_per_word = 16;
	xfer.len = 2;
	spi_message_add_tail(&xfer, &m);

	r = spi_sync(spi, &m);
	if (r < 0)
		dev_warn(&spi->dev, "failed to write to LCD reg (%d)\n", r);
}

static void tpo_td043_write_gamma(struct spi_device *spi, u16 gamma[12])
{
	u8 i, val;

	/* gamma bits [9:8] */
	for (val = i = 0; i < 4; i++)
		val |= (gamma[i] & 0x300) >> ((i + 1) * 2);
	tpo_td043_write(spi, 0x11, val);

	for (val = i = 0; i < 4; i++)
		val |= (gamma[i+4] & 0x300) >> ((i + 1) * 2);
	tpo_td043_write(spi, 0x12, val);

	for (val = i = 0; i < 4; i++)
		val |= (gamma[i+8] & 0x300) >> ((i + 1) * 2);
	tpo_td043_write(spi, 0x13, val);

	/* gamma bits [7:0] */
	for (val = i = 0; i < 12; i++)
		tpo_td043_write(spi, 0x14 + i, gamma[i] & 0xff);
}

static int tpo_td043_panel_init(struct omap_display *display)
{
	int nreset_gpio = display->hw_config.panel_reset_gpio;
	int ret = 0;

	if (gpio_is_valid(nreset_gpio)) {
		ret = gpio_request(nreset_gpio, "lcd reset");
		if (ret < 0) {
			pr_err("tpo_td043: couldn't request reset GPIO\n");
			return ret;
		}

		ret = gpio_direction_output(nreset_gpio, 0);
		if (ret < 0)
			pr_err("tpo_td043: couldn't set GPIO direction\n");
	}

	return ret;
}

static int tpo_td043_panel_enable(struct omap_display *display)
{
	struct tpo_td043_device *tpo_td043 = display->panel->priv;
	int nreset_gpio = display->hw_config.panel_reset_gpio;
	int ret;

	if (display->hw_config.panel_enable) {
		ret = display->hw_config.panel_enable(display);
		if (ret < 0)
			return ret;
	}

	/* wait for power up */
	msleep(160);

	if (gpio_is_valid(nreset_gpio))
		gpio_set_value(nreset_gpio, 1);

	tpo_td043_write(tpo_td043->spi, 2,
			TPO_R02_MODE(tpo_td043->mode) | TPO_R02_NCLK_RISING);
	tpo_td043_write(tpo_td043->spi, 3, TPO_R03_VAL_NORMAL);
	tpo_td043_write(tpo_td043->spi, 4, (tpo_td043->mirror ^ 3) |
			TPO_R04_CP_CLK_FREQ_1H | TPO_R04_VGL_FREQ_1H);
	tpo_td043_write(tpo_td043->spi, 0x20, 0xf0);
	tpo_td043_write(tpo_td043->spi, 0x21, 0xf0);
	tpo_td043_write_gamma(tpo_td043->spi, tpo_td043->gamma);

	return 0;
}

static void tpo_td043_panel_disable(struct omap_display *display)
{
	struct tpo_td043_device *tpo_td043 = display->panel->priv;
	int nreset_gpio = display->hw_config.panel_reset_gpio;

	tpo_td043_write(tpo_td043->spi, 3,
			TPO_R03_VAL_STANDBY | TPO_R03_EN_PWM);

	if (gpio_is_valid(nreset_gpio))
		gpio_set_value(nreset_gpio, 0);

	/* wait for at least 2 vsyncs before cutting off power */
	msleep(50);

	tpo_td043_write(tpo_td043->spi, 3, TPO_R03_VAL_STANDBY);

	if (display->hw_config.panel_disable)
		display->hw_config.panel_disable(display);
}

static int tpo_td043_panel_suspend(struct omap_display *display)
{
	tpo_td043_panel_disable(display);
	return 0;
}

static int tpo_td043_panel_resume(struct omap_display *display)
{
	return tpo_td043_panel_enable(display);
}

static void tpo_td043_panel_cleanup(struct omap_display *display)
{
	int nreset_gpio = display->hw_config.panel_reset_gpio;

	if (gpio_is_valid(nreset_gpio))
		gpio_free(nreset_gpio);
}

static struct omap_panel tpo_td043_panel = {
	.owner		= THIS_MODULE,
	.name		= "tpo-td043mtea1",
	.init		= tpo_td043_panel_init,
	.enable		= tpo_td043_panel_enable,
	.disable	= tpo_td043_panel_disable,
	.suspend	= tpo_td043_panel_suspend,
	.resume		= tpo_td043_panel_resume,
	.cleanup	= tpo_td043_panel_cleanup,

	.timings = {
		.x_res = 800,
		.y_res = 480,

		.pixel_clock	= 36000,

		.hsw		= 1,
		.hfp		= 68,
		.hbp		= 214,

		.vsw		= 1,
		.vfp		= 39,
		.vbp		= 34,
	},

	/* for now, to have SGX working */
	.recommended_bpp = 16,

	.config = OMAP_DSS_LCD_TFT | OMAP_DSS_LCD_IHS |
		OMAP_DSS_LCD_IVS | OMAP_DSS_LCD_IPC,
};

static ssize_t tpo_td043_mirror_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tpo_td043_device *tpo_td043 = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", tpo_td043->mirror);
}

static ssize_t tpo_td043_mirror_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct tpo_td043_device *tpo_td043 = dev_get_drvdata(dev);
	long val;

	val = simple_strtol(buf, NULL, 0);
	if (val & ~3)
		return -EINVAL;

	tpo_td043->mirror = val;

	val ^= 3;
	val |= TPO_R04_CP_CLK_FREQ_1H | TPO_R04_VGL_FREQ_1H;
	tpo_td043_write(tpo_td043->spi, 4, val);

	return count;
}

static ssize_t tpo_td043_mode_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tpo_td043_device *tpo_td043 = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", tpo_td043->mode);
}

static ssize_t tpo_td043_mode_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct tpo_td043_device *tpo_td043 = dev_get_drvdata(dev);
	long val;

	val = simple_strtol(buf, NULL, 0);
	if (val & ~7)
		return -EINVAL;

	tpo_td043->mode = val;

	val |= TPO_R02_NCLK_RISING;
	tpo_td043_write(tpo_td043->spi, 2, val);

	return count;
}

static ssize_t tpo_td043_gamma_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct tpo_td043_device *tpo_td043 = dev_get_drvdata(dev);
	ssize_t len = 0;
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(tpo_td043->gamma); i++) {
		ret = snprintf(buf + len, PAGE_SIZE - len, "%u ",
				tpo_td043->gamma[i]);
		if (ret < 0)
			return ret;
		len += ret;
	}
	buf[len - 1] = '\n';

	return len;
}

static ssize_t tpo_td043_gamma_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct tpo_td043_device *tpo_td043 = dev_get_drvdata(dev);
	unsigned int g[12];
	int ret;
	int i;

	ret = sscanf(buf, "%u %u %u %u %u %u %u %u %u %u %u %u",
			&g[0], &g[1], &g[2], &g[3], &g[4], &g[5],
			&g[6], &g[7], &g[8], &g[9], &g[10], &g[11]);

	if (ret != 12)
		return -EINVAL;

	for (i = 0; i < 12; i++)
		tpo_td043->gamma[i] = g[i];

	tpo_td043_write_gamma(tpo_td043->spi, tpo_td043->gamma);

	return count;
}

static ssize_t tpo_td043_reg_write(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct tpo_td043_device *tpo_td043 = dev_get_drvdata(dev);
	unsigned long reg, val;
	int ret;

	ret = sscanf(buf, "%lx %lx", &reg, &val);
	if (ret != 2 || (reg & ~0x3f) || (val & ~0xff))
		return -EINVAL;

	tpo_td043_write(tpo_td043->spi, reg, val);

	return count;
}

static DEVICE_ATTR(mirror, 0664, tpo_td043_mirror_show, tpo_td043_mirror_store);
static DEVICE_ATTR(mode, 0664, tpo_td043_mode_show, tpo_td043_mode_store);
static DEVICE_ATTR(gamma, 0664, tpo_td043_gamma_show, tpo_td043_gamma_store);
static DEVICE_ATTR(reg_write, 0200, NULL, tpo_td043_reg_write);

static int tpo_td043_spi_probe(struct spi_device *spi)
{
	struct tpo_td043_device *tpo_td043;
	int ret;

	spi->bits_per_word = 16;
	spi->mode = SPI_MODE_0;

	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "spi_setup failed: %d\n", ret);
		return ret;
	}

	tpo_td043 = kzalloc(sizeof(*tpo_td043), GFP_KERNEL);
	if (tpo_td043 == NULL)
		return -ENOMEM;

	tpo_td043->spi = spi;
	tpo_td043->mode = 7;
	memcpy(tpo_td043->gamma, tpo_td043_def_gamma, sizeof(tpo_td043->gamma));

	dev_set_drvdata(&spi->dev, tpo_td043);
	tpo_td043_panel.priv = tpo_td043;

	omap_dss_register_panel(&tpo_td043_panel);

	ret = device_create_file(&spi->dev, &dev_attr_mirror);
	ret |= device_create_file(&spi->dev, &dev_attr_mode);
	ret |= device_create_file(&spi->dev, &dev_attr_gamma);
	ret |= device_create_file(&spi->dev, &dev_attr_reg_write);
	if (ret < 0)
		dev_warn(&spi->dev, "failed to add sysfs file(s)\n");

	return 0;
}

static int tpo_td043_spi_remove(struct spi_device *spi)
{
	struct tpo_td043_device *tpo_td043 = dev_get_drvdata(&spi->dev);

	device_remove_file(&spi->dev, &dev_attr_reg_write);
	device_remove_file(&spi->dev, &dev_attr_gamma);
	device_remove_file(&spi->dev, &dev_attr_mode);
	device_remove_file(&spi->dev, &dev_attr_mirror);
	omap_dss_unregister_panel(&tpo_td043_panel);

	kfree(tpo_td043);

	return 0;
}

static struct spi_driver tpo_td043_spi_driver = {
	.driver = {
		.name	= "panel-tpo-td043mtea1",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe	= tpo_td043_spi_probe,
	.remove	= __devexit_p(tpo_td043_spi_remove),
};

static int __init tpo_td043_init(void)
{
	return spi_register_driver(&tpo_td043_spi_driver);
}

static void __exit tpo_td043_exit(void)
{
	spi_unregister_driver(&tpo_td043_spi_driver);
}

module_init(tpo_td043_init);
module_exit(tpo_td043_exit);

MODULE_AUTHOR("Gražvydas Ignotas <notasas@gmail.com>");
MODULE_DESCRIPTION("TPO TD043MTEA1 LCD Driver");
MODULE_LICENSE("GPL");
