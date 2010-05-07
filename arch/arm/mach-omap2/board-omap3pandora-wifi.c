/*
 * board-omap3pandora-wifi.c
 *
 * WiFi setup (SDIO) for Pandora handheld console
 * Author: John Willis <John.Willis@Distant-earth.com>
 *
 * Based on /arch/arm/mach-msm/msm_wifi.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/spi/wl12xx.h>

extern void omap_mmc_fake_detect_mmc3(int is_in);

static int wifi_probe(struct platform_device *pdev)
{
	struct wl12xx_platform_data *wifi_data = pdev->dev.platform_data;

	printk(KERN_DEBUG "Pandora WiFi: Probe started\n");

	if (!wifi_data)
		return -ENODEV;

	if (wifi_data->set_power)
		wifi_data->set_power(1);	/* Power On */
	msleep(20);
	omap_mmc_fake_detect_mmc3(1);

	printk(KERN_DEBUG "Pandora WiFi: Probe done\n");
	return 0;
}

static int wifi_remove(struct platform_device *pdev)
{
	struct wl12xx_platform_data *wifi_data = pdev->dev.platform_data;

	printk(KERN_DEBUG "Pandora WiFi: Remove started\n");
	if (!wifi_data)
		return -ENODEV;

	omap_mmc_fake_detect_mmc3(0);
	msleep(20);
	if (wifi_data->set_power)
		wifi_data->set_power(0);	/* Power Off */

	printk(KERN_DEBUG "Pandora WiFi: Remove finished\n");
	return 0;
}

static struct platform_driver wifi_device = {
	.probe		= wifi_probe,
	.remove		= wifi_remove,
	.driver		= {
		.name   = "pandora_wifi",
	},
};

static int __init pandora_wifi_sdio_init(void)
{
	return platform_driver_register(&wifi_device);
}

static void __exit pandora_wifi_sdio_exit(void)
{
	platform_driver_unregister(&wifi_device);
}

module_init(pandora_wifi_sdio_init);
module_exit(pandora_wifi_sdio_exit);
MODULE_LICENSE("GPL");
