/*
 * board-omap3pandora.c (Pandora Handheld Console)
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

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>
#include <linux/i2c/twl4030.h>
#include <linux/i2c/vsense.h>
#include <linux/leds.h>
#include <linux/leds_pwm.h>
#include <linux/spi/wl12xx.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/map.h>

#include <mach/board.h>
#include <mach/common.h>
#include <mach/gpio.h>
#include <mach/gpmc.h>
#include <mach/hardware.h>
#include <mach/hsmmc.h>
#include <mach/nand.h>
#include <mach/usb-ehci.h>
#include <mach/usb-musb.h>
#include <mach/mcspi.h>
#include <mach/display.h>

#include "sdram-micron-mt46h32m32lf-6.h"

#define NAND_BLOCK_SIZE SZ_128K
#define GPMC_CS0_BASE  0x60
#define GPMC_CS_SIZE   0x30

static struct mtd_partition omap3pandora_nand_partitions[] = {
	{
		.name           = "xloader",
		.offset         = 0,			/* Offset = 0x00000 */
		.size           = 4 * NAND_BLOCK_SIZE,
		.mask_flags     = MTD_WRITEABLE
	}, {
		.name           = "uboot",
		.offset         = MTDPART_OFS_APPEND,	/* Offset = 0x80000 */
		.size           = 14 * NAND_BLOCK_SIZE,
	}, {
		.name           = "uboot environment",
		.offset         = MTDPART_OFS_APPEND,	/* Offset = 0x240000 */
		.size           = 2 * NAND_BLOCK_SIZE,
	}, {
		.name           = "linux",
		.offset         = MTDPART_OFS_APPEND,	/* Offset = 0x280000 */
		.size           = 32 * NAND_BLOCK_SIZE,
	}, {
		.name           = "rootfs",
		.offset         = MTDPART_OFS_APPEND,	/* Offset = 0x680000 */
		.size           = MTDPART_SIZ_FULL,
	},
};

static struct omap_nand_platform_data omap3pandora_nand_data = {
	.parts		= omap3pandora_nand_partitions,
	.nr_parts	= ARRAY_SIZE(omap3pandora_nand_partitions),
	.dma_channel	= -1,	/* disable DMA in OMAP NAND driver */
};

static struct resource omap3pandora_nand_resource[] = {
	{
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device omap3pandora_nand_device = {
	.name		= "omap2-nand",
	.id		= -1,
	.dev		= {
		.platform_data	= &omap3pandora_nand_data,
	},
	.num_resources	= ARRAY_SIZE(omap3pandora_nand_resource),
	.resource	= omap3pandora_nand_resource,
};

static void __init omap3pandora_flash_init(void)
{
	u8 cs = 0;
	u8 nandcs = GPMC_CS_NUM + 1;

	u32 gpmc_base_add = OMAP34XX_GPMC_VIRT;

	/* find out the chip-select on which NAND exists */
	while (cs < GPMC_CS_NUM) {
		u32 ret = 0;
		ret = gpmc_cs_read_reg(cs, GPMC_CS_CONFIG1);

		if ((ret & 0xC00) == 0x800) {
			printk(KERN_INFO "Found NAND on CS%d\n", cs);
			if (nandcs > GPMC_CS_NUM)
				nandcs = cs;
		}
		cs++;
	}

	if (nandcs > GPMC_CS_NUM) {
		printk(KERN_INFO "NAND: Unable to find configuration "
				 "in GPMC\n ");
		return;
	}

	if (nandcs < GPMC_CS_NUM) {
		omap3pandora_nand_data.cs = nandcs;
		omap3pandora_nand_data.gpmc_cs_baseaddr = (void *)
			(gpmc_base_add + GPMC_CS0_BASE + nandcs * GPMC_CS_SIZE);
		omap3pandora_nand_data.gpmc_baseaddr = (void *) (gpmc_base_add);

		printk(KERN_INFO "Registering NAND on CS%d\n", nandcs);
		if (platform_device_register(&omap3pandora_nand_device) < 0)
			printk(KERN_ERR "Unable to register NAND device\n");
	}
}

static struct omap_uart_config omap3pandora_uart_config __initdata = {
	.enabled_uarts	= (1 << 2), /* UART3 */
};

static struct gpio_led omap3pandora_gpio_leds[] = {
	{
		.name			= "pandora::sd1",
		.default_trigger	= "mmc0",
		.gpio			= 128,
	}, {
		.name			= "pandora::sd2",
		.default_trigger	= "mmc1",
		.gpio			= 129,
	}, {
		.name			= "pandora::bluetooth",
		.default_trigger	= "bluetooth",
		.gpio			= 158,
	}, {
		.name			= "pandora::wifi",
		.gpio			= 159,
	},
};

static struct gpio_led_platform_data omap3pandora_gpio_led_data = {
	.leds		= omap3pandora_gpio_leds,
	.num_leds	= ARRAY_SIZE(omap3pandora_gpio_leds),
};

static struct platform_device omap3pandora_leds_gpio = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &omap3pandora_gpio_led_data,
	},
};

static struct led_pwm pandora_pwm_leds[] = {
	{
		.name			= "pandora::keypad_bl",
		.pwm_id			= 0, /* LEDA */
	}, {
		.name			= "pandora::power",
		.pwm_id			= 1, /* LEDB */
	}, {
		.name			= "pandora::charger",
		.default_trigger	= "twl4030_bci_battery-charging",
		.pwm_id			= 3, /* PWM1 */
	}
};

static struct led_pwm_platform_data pandora_pwm_led_data = {
	.leds		= pandora_pwm_leds,
	.num_leds	= ARRAY_SIZE(pandora_pwm_leds),
};

static struct platform_device pandora_leds_pwm = {
	.name	= "leds-twl4030-pwm",
	.id	= -1,
	.dev	= {
		.platform_data	= &pandora_pwm_led_data,
	},
};

static struct platform_device omap3pandora_bl = {
	.name	= "twl4030-pwm0-bl",
	.id	= -1,
};

static int omap3pandora_twl_gpio_setup(struct device *dev,
		unsigned gpio, unsigned ngpio)
{
	int ret, gpio_32khz;

	/* hack */
	gpio_32khz = gpio + 13;
	ret = gpio_request(gpio_32khz, "32kHz");
	if (ret != 0)
		printk(KERN_ERR "Cannot get GPIO line %d\n", gpio_32khz);
	ret = gpio_direction_output(gpio_32khz, 1);
	if (ret != 0)
		printk(KERN_ERR "Cannot set GPIO line %d\n", gpio_32khz);
	else
		printk(KERN_INFO "TWL GPIO 13 (GPIO %d) set.\n", gpio_32khz);
	gpio_free(gpio_32khz);

	return 0;
}

static struct twl4030_gpio_platform_data omap3pandora_gpio_data = {
	.gpio_base	= OMAP_MAX_GPIO_LINES,
	.irq_base	= TWL4030_GPIO_IRQ_BASE,
	.irq_end	= TWL4030_GPIO_IRQ_END,
	.use_leds	= true,
	.setup		= omap3pandora_twl_gpio_setup,
};

static struct twl4030_usb_data omap3pandora_usb_data = {
	.usb_mode	= T2_USB_MODE_ULPI,
};

static int pandora_batt_table[] = { 
	/* 0 C*/
	30800, 29500, 28300, 27100,
	26000, 24900, 23900, 22900, 22000, 21100, 20300, 19400, 18700, 17900,
	17200, 16500, 15900, 15300, 14700, 14100, 13600, 13100, 12600, 12100,
	11600, 11200, 10800, 10400, 10000, 9630,   9280,   8950,   8620,   8310,
	8020,   7730,   7460,   7200,   6950,   6710,   6470,   6250,   6040,   5830,
	5640,   5450,   5260,   5090,   4920,   4760,   4600,   4450,   4310,   4170,
	4040,   3910,   3790,   3670,   3550
};

static struct twl4030_bci_platform_data pandora_bci_data = { 
        .battery_tmp_tbl	= pandora_batt_table,
        .tblsize		= ARRAY_SIZE(pandora_batt_table),
};

static struct twl4030_madc_platform_data pandora_madc_data = {
	.irq_line	= 1,
};

static struct twl4030_platform_data omap3pandora_twldata = {
	.irq_base	= TWL4030_IRQ_BASE,
	.irq_end	= TWL4030_IRQ_END,
	.bci		= &pandora_bci_data,
	.madc		= &pandora_madc_data,
	.gpio		= &omap3pandora_gpio_data,
	.usb		= &omap3pandora_usb_data,
	.keypad		= &omap3pandora_kp_data,
};

static struct i2c_board_info __initdata omap3pandora_i2c_boardinfo[] = {
	{
		I2C_BOARD_INFO("twl4030", 0x48),
		.flags = I2C_CLIENT_WAKE,
		.irq = INT_34XX_SYS_NIRQ,
		.platform_data = &omap3pandora_twldata,
	},
};

static struct vsense_platform_data omap3pandora_nub1_data = {
	.gpio_irq	= 161,
	.gpio_reset	= 156,
};

static struct vsense_platform_data omap3pandora_nub2_data = {
	.gpio_irq	= 162,
	.gpio_reset	= 156,
};

static struct i2c_board_info __initdata omap3pandora_i2c3_boardinfo[] = {
	{
		I2C_BOARD_INFO("vsense", 0x66),
		.flags = I2C_CLIENT_WAKE,
		.platform_data = &omap3pandora_nub1_data,
	}, {
		I2C_BOARD_INFO("vsense", 0x67),
		.flags = I2C_CLIENT_WAKE,
		.platform_data = &omap3pandora_nub2_data,
	}, {
		I2C_BOARD_INFO("bq27500", 0x55),
		.flags = I2C_CLIENT_WAKE,
	},
};

static int __init omap3pandora_i2c_init(void)
{
	omap_register_i2c_bus(1, 2600, omap3pandora_i2c_boardinfo,
			ARRAY_SIZE(omap3pandora_i2c_boardinfo));
	/* i2c2 pins are not connected */
	omap_register_i2c_bus(3, 100, omap3pandora_i2c3_boardinfo,
			ARRAY_SIZE(omap3pandora_i2c3_boardinfo));
	return 0;
}

static void __init omap3pandora_init_irq(void)
{
	omap2_init_common_hw(mt46h32m32lf6_sdrc_params);
	omap_init_irq();
	omap_gpio_init();
}

static void __init omap3pandora_ads7846_init(void)
{
	int gpio = OMAP3_PANDORA_TS_GPIO;
	int ret;

	ret = gpio_request(gpio, "ads7846_pen_down");
	if (ret < 0) {
		printk(KERN_ERR "Failed to request GPIO %d for "
				"ads7846 pen down IRQ\n", gpio);
		return;
	}

	gpio_direction_input(gpio);
}

static int ads7846_get_pendown_state(void)
{
	return !omap_get_gpio_datain(OMAP3_PANDORA_TS_GPIO);
}

static struct ads7846_platform_data ads7846_config = {
	.x_max			= 0x0fff,
	.y_max			= 0x0fff,
	.x_plate_ohms		= 180,
	.pressure_max		= 255,
	.debounce_max		= 10,
	.debounce_tol		= 3,
	.debounce_rep		= 1,
	.get_pendown_state	= ads7846_get_pendown_state,
	.keep_vref_on		= 1,
};

static struct omap2_mcspi_device_config ads7846_mcspi_config = {
	.turbo_mode	= 0,
	.single_channel	= 1,  /* 0: slave, 1: master */
};

static struct spi_board_info omap3pandora_spi_board_info[] = {
	{
		.modalias		= "ads7846",
		.bus_num		= 1,
		.chip_select		= 0,
		.max_speed_hz		= 1500000,
		.controller_data	= &ads7846_mcspi_config,
		.irq			= OMAP_GPIO_IRQ(OMAP3_PANDORA_TS_GPIO),
		.platform_data		= &ads7846_config,
	}, {
		.modalias		= "panel-tpo-td043mtea1",
		.bus_num		= 1,
		.chip_select		= 1,
		.max_speed_hz		= 375000,
	}
};

static struct platform_device omap3pandora_lcd_device = {
	.name		= "pandora_lcd",
	.id		= -1,
};

static struct omap_lcd_config omap3pandora_lcd_config __initdata = {
	.ctrl_name	= "internal",
};

static struct omap_board_config_kernel omap3pandora_config[] __initdata = {
	{ OMAP_TAG_UART,	&omap3pandora_uart_config },
	{ OMAP_TAG_LCD,		&omap3pandora_lcd_config },
};

/* DSS2 */
static int pandora_panel_enable_lcd(struct omap_display *display)
{
#define ENABLE_VAUX1_DEDICATED           0x04
#define ENABLE_VAUX1_DEV_GRP             0x20

	twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
			ENABLE_VAUX1_DEDICATED,
			TWL4030_VAUX1_DEDICATED);
	twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
			ENABLE_VAUX1_DEV_GRP, TWL4030_VAUX1_DEV_GRP);

	return 0;
}

static void pandora_panel_disable_lcd(struct omap_display *display)
{
	twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, 0x00,
			TWL4030_VAUX1_DEV_GRP);
	twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, 0x00,
			TWL4030_VAUX1_DEDICATED);
}

static int pandora_panel_enable_tv(struct omap_display *display)
{
#define ENABLE_VDAC_DEDICATED           0x03
#define ENABLE_VDAC_DEV_GRP             0x20

	twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
			ENABLE_VDAC_DEDICATED,
			TWL4030_VDAC_DEDICATED);
	twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
			ENABLE_VDAC_DEV_GRP, TWL4030_VDAC_DEV_GRP);

	return 0;
}

static void pandora_panel_disable_tv(struct omap_display *display)
{
	twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, 0x00,
			TWL4030_VDAC_DEV_GRP);
	twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, 0x00,
			TWL4030_VDAC_DEDICATED);
}

static struct omap_dss_display_config omap3pandora_display_data[] = {
	{
		.type		= OMAP_DISPLAY_TYPE_DPI,
		.name		= "lcd",
		.panel_name	= "tpo-td043mtea1",
		.panel_reset_gpio = 157,
		.u.dpi.data_lines = 24,
		.panel_enable	= pandora_panel_enable_lcd,
		.panel_disable	= pandora_panel_disable_lcd,
	}, {
		.type		= OMAP_DISPLAY_TYPE_VENC,
		.name		= "tv",
		.u.venc.type	= OMAP_DSS_VENC_TYPE_SVIDEO,
		.panel_enable	= pandora_panel_enable_tv,
		.panel_disable	= pandora_panel_disable_tv,
	}
};

static struct omap_dss_board_info omap3pandora_dss_data = {
	.num_displays = ARRAY_SIZE(omap3pandora_display_data),
	.displays = {
		&omap3pandora_display_data[0],
		&omap3pandora_display_data[1],
	}
};

static struct platform_device omap3pandora_dss_device = {
	.name	= "omapdss",
	.id	= -1,
	.dev	= {
		.platform_data = &omap3pandora_dss_data,
	},
};

#include <mach/board-nokia.h>

static struct omap_bluetooth_config bt_config = {
	.chip_type		= BT_CHIP_TI,
	.bt_wakeup_gpio		= -EINVAL,
	.host_wakeup_gpio	= -EINVAL,
	.reset_gpio		= 15,
	.bt_uart		= 1,
//	.bd_addr[6],
//	.bt_sysclk,	/* unused? */
};

static struct platform_device bt_device = {
	.name           = "hci_h4p",
	.id             = -1,
	.dev		= {
		.platform_data	= &bt_config,
	},
};

#define PANDORA_WIFI_NRESET_GPIO	23
#define PANDORA_WIFI_IRQ_GPIO		21

/* platform_device for the wl1251 driver */
static void wl1251_set_power(bool enable)
{
	gpio_set_value(PANDORA_WIFI_NRESET_GPIO, enable);
}

static struct wl12xx_platform_data wl1251_data = {
	.set_power	= wl1251_set_power,
	.use_eeprom	= true,
};

static struct platform_device wl1251_data_device = {
	.name           = "wl1251_data",
	.id             = -1,
	.dev		= {
		.platform_data	= &wl1251_data,
	},
};

static void pandora_wl1251_init(void)
{
	int ret;

	ret = gpio_request(PANDORA_WIFI_NRESET_GPIO, "wl1251 nreset");
	if (ret < 0)
		goto fail;

	ret = gpio_direction_output(PANDORA_WIFI_NRESET_GPIO, 0);
	if (ret < 0)
		goto fail_nreset;

	ret = gpio_request(PANDORA_WIFI_IRQ_GPIO, "wl1251 irq");
	if (ret < 0)
		goto fail_nreset;

	ret = gpio_direction_input(PANDORA_WIFI_IRQ_GPIO);
	if (ret < 0)
		goto fail_irq;

	wl1251_data.irq = gpio_to_irq(PANDORA_WIFI_IRQ_GPIO);
	if (wl1251_data.irq < 0)
		goto fail_irq;

	return;

fail_irq:
	gpio_free(PANDORA_WIFI_IRQ_GPIO);
fail_nreset:
	gpio_free(PANDORA_WIFI_NRESET_GPIO);
fail:
	printk(KERN_ERR "wl1251 board initialisation failed\n");
}

/* platform_device for fake card detect 'driver' */
static struct platform_device wl1251_cd_device = {
	.name           = "pandora_wifi",
	.id             = -1,
	.dev		= {
		.platform_data	= &wl1251_data,
	},
};

static struct platform_device *omap3pandora_devices[] __initdata = {
	&omap3pandora_lcd_device,
	&omap3pandora_leds_gpio,
	&bt_device,
	&omap3pandora_bl,
	&omap3pandora_dss_device,
	&pandora_leds_pwm,
	&wl1251_data_device,
	&wl1251_cd_device,
};

static void __init omap3pandora_init(void)
{
	omap3pandora_i2c_init();
	omap3pandora_input_init();
	pandora_wl1251_init();
	platform_add_devices(omap3pandora_devices, ARRAY_SIZE(omap3pandora_devices));
	omap_board_config = omap3pandora_config;
	omap_board_config_size = ARRAY_SIZE(omap3pandora_config);
	spi_register_board_info(omap3pandora_spi_board_info,
			ARRAY_SIZE(omap3pandora_spi_board_info));
	omap_serial_init();
	hsmmc_init();
	usb_musb_init();
	usb_ehci_init();
	omap3pandora_flash_init();
	omap3pandora_ads7846_init();
}

static void __init omap3pandora_map_io(void)
{
	omap2_set_globals_343x();
	omap2_map_common_io();
}

MACHINE_START(OMAP3_PANDORA, "Pandora Handheld Console")
	.phys_io	= 0x48000000,
	.io_pg_offst	= ((0xd8000000) >> 18) & 0xfffc,
	.boot_params	= 0x80000100,
	.map_io		= omap3pandora_map_io,
	.init_irq	= omap3pandora_init_irq,
	.init_machine	= omap3pandora_init,
	.timer		= &omap_timer,
MACHINE_END
