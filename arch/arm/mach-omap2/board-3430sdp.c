/*
 * linux/arch/arm/mach-omap2/board-3430sdp.c
 *
 * Copyright (C) 2007 Texas Instruments
 *
 * Modified from mach-omap2/board-generic.c
 *
 * Initial code: Syed Mohammed Khasim
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>
#include <linux/i2c/twl4030.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/mcspi.h>
#include <mach/gpio.h>
#include <mach/mux.h>
#include <mach/board.h>
#include <mach/usb-musb.h>
#include <mach/usb-ehci.h>
#include <mach/hsmmc.h>
#include <mach/common.h>
#include <mach/keypad.h>
#include <mach/dma.h>
#include <mach/gpmc.h>
#include <mach/display.h>

#include <asm/io.h>
#include <asm/delay.h>
#include <mach/control.h>

#include "sdram-qimonda-hyb18m512160af-6.h"

#define	SDP3430_SMC91X_CS	3

#define ENABLE_VAUX3_DEDICATED	0x03
#define ENABLE_VAUX3_DEV_GRP	0x20


#define TWL4030_MSECURE_GPIO 22

static struct resource sdp3430_smc91x_resources[] = {
	[0] = {
		.start	= OMAP34XX_ETHR_START,
		.end	= OMAP34XX_ETHR_START + SZ_4K,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= 0,
		.end	= 0,
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	},
};

static struct platform_device sdp3430_smc91x_device = {
	.name		= "smc91x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(sdp3430_smc91x_resources),
	.resource	= sdp3430_smc91x_resources,
};

static int sdp3430_keymap[] = {
	KEY(0, 0, KEY_LEFT),
	KEY(0, 1, KEY_RIGHT),
	KEY(0, 2, KEY_A),
	KEY(0, 3, KEY_B),
	KEY(0, 4, KEY_C),
	KEY(1, 0, KEY_DOWN),
	KEY(1, 1, KEY_UP),
	KEY(1, 2, KEY_E),
	KEY(1, 3, KEY_F),
	KEY(1, 4, KEY_G),
	KEY(2, 0, KEY_ENTER),
	KEY(2, 1, KEY_I),
	KEY(2, 2, KEY_J),
	KEY(2, 3, KEY_K),
	KEY(2, 4, KEY_3),
	KEY(3, 0, KEY_M),
	KEY(3, 1, KEY_N),
	KEY(3, 2, KEY_O),
	KEY(3, 3, KEY_P),
	KEY(3, 4, KEY_Q),
	KEY(4, 0, KEY_R),
	KEY(4, 1, KEY_4),
	KEY(4, 2, KEY_T),
	KEY(4, 3, KEY_U),
	KEY(4, 4, KEY_D),
	KEY(5, 0, KEY_V),
	KEY(5, 1, KEY_W),
	KEY(5, 2, KEY_L),
	KEY(5, 3, KEY_S),
	KEY(5, 4, KEY_H),
	0
};

static struct twl4030_keypad_data sdp3430_kp_data = {
	.rows		= 5,
	.cols		= 6,
	.keymap		= sdp3430_keymap,
	.keymapsize	= ARRAY_SIZE(sdp3430_keymap),
	.rep		= 1,
	.irq		= TWL4030_MODIRQ_KEYPAD,
};

static int ts_gpio;

static int __init msecure_init(void)
{
	int ret = 0;

#ifdef CONFIG_RTC_DRV_TWL4030
	/* 3430ES2.0 doesn't have msecure/gpio-22 line connected to T2 */
	if (omap_type() == OMAP2_DEVICE_TYPE_GP &&
			system_rev < OMAP3430_REV_ES2_0) {
		void __iomem *msecure_pad_config_reg = omap_ctrl_base_get() +
			0xA3C;
		int mux_mask = 0x04;
		u16 tmp;

		ret = gpio_request(TWL4030_MSECURE_GPIO, "msecure");
		if (ret < 0) {
			printk(KERN_ERR "msecure_init: can't"
				"reserve GPIO:%d !\n", TWL4030_MSECURE_GPIO);
			goto out;
		}
		/*
		 * TWL4030 will be in secure mode if msecure line from OMAP
		 * is low. Make msecure line high in order to change the
		 * TWL4030 RTC time and calender registers.
		 */
		tmp = __raw_readw(msecure_pad_config_reg);
		tmp &= 0xF8; /* To enable mux mode 03/04 = GPIO_RTC */
		tmp |= mux_mask;/* To enable mux mode 03/04 = GPIO_RTC */
		__raw_writew(tmp, msecure_pad_config_reg);

		gpio_direction_output(TWL4030_MSECURE_GPIO, 1);
	}
out:
#endif
	return ret;
}

/**
 * @brief ads7846_dev_init : Requests & sets GPIO line for pen-irq
 *
 * @return - void. If request gpio fails then Flag KERN_ERR.
 */
static void ads7846_dev_init(void)
{
	if (omap_request_gpio(ts_gpio) < 0) {
		printk(KERN_ERR "can't get ads746 pen down GPIO\n");
		return;
	}

	omap_set_gpio_direction(ts_gpio, 1);

	omap_set_gpio_debounce(ts_gpio, 1);
	omap_set_gpio_debounce_time(ts_gpio, 0xa);
}

static int ads7846_get_pendown_state(void)
{
	return !omap_get_gpio_datain(ts_gpio);
}

/*
 * This enable(1)/disable(0) the voltage for TS: uses twl4030 calls
 */
static int ads7846_vaux_control(int vaux_cntrl)
{
	int ret = 0;

#ifdef CONFIG_TWL4030_CORE
	/* check for return value of ldo_use: if success it returns 0 */
	if (vaux_cntrl == VAUX_ENABLE) {
		if (ret != twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
			ENABLE_VAUX3_DEDICATED, TWL4030_VAUX3_DEDICATED))
			return -EIO;
		if (ret != twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
			ENABLE_VAUX3_DEV_GRP, TWL4030_VAUX3_DEV_GRP))
			return -EIO;
	} else if (vaux_cntrl == VAUX_DISABLE) {
		if (ret != twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
			0x00, TWL4030_VAUX3_DEDICATED))
			return -EIO;
		if (ret != twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
			0x00, TWL4030_VAUX3_DEV_GRP))
			return -EIO;
	}
#else
	ret = -EIO;
#endif
	return ret;
}

static struct ads7846_platform_data tsc2046_config __initdata = {
	.get_pendown_state	= ads7846_get_pendown_state,
	.keep_vref_on		= 1,
	.vaux_control		= ads7846_vaux_control,
};


static struct omap2_mcspi_device_config tsc2046_mcspi_config = {
	.turbo_mode	= 0,
	.single_channel	= 1,  /* 0: slave, 1: master */
};

static struct spi_board_info sdp3430_spi_board_info[] __initdata = {
	[0] = {
		/*
		 * TSC2046 operates at a max freqency of 2MHz, so
		 * operate slightly below at 1.5MHz
		 */
		.modalias		= "ads7846",
		.bus_num		= 1,
		.chip_select		= 0,
		.max_speed_hz		= 1500000,
		.controller_data	= &tsc2046_mcspi_config,
		.irq			= 0,
		.platform_data		= &tsc2046_config,
	},
};


#define SDP2430_LCD_PANEL_BACKLIGHT_GPIO	91
#define SDP2430_LCD_PANEL_ENABLE_GPIO		154
#if 0
#define SDP3430_LCD_PANEL_BACKLIGHT_GPIO	24
#define SDP3430_LCD_PANEL_ENABLE_GPIO		28
#else
#define SDP3430_LCD_PANEL_BACKLIGHT_GPIO	8
#define SDP3430_LCD_PANEL_ENABLE_GPIO		5
#endif

#define PM_RECEIVER             TWL4030_MODULE_PM_RECEIVER
#define ENABLE_VAUX2_DEDICATED  0x09
#define ENABLE_VAUX2_DEV_GRP    0x20
#define ENABLE_VAUX3_DEDICATED	0x03
#define ENABLE_VAUX3_DEV_GRP	0x20

#define ENABLE_VPLL2_DEDICATED	0x05
#define ENABLE_VPLL2_DEV_GRP	0xE0
#define TWL4030_VPLL2_DEV_GRP	0x33
#define TWL4030_VPLL2_DEDICATED	0x36

#define t2_out(c, r, v) twl4030_i2c_write_u8(c, r, v)

static unsigned backlight_gpio;
static unsigned enable_gpio;
static int lcd_enabled;
static int dvi_enabled;

static struct platform_device sdp3430_lcd_device = {
	.name		= "sdp2430_lcd",
	.id		= -1,
};

static void enable_vpll2(int enable)
{
	u8 ded_val, grp_val;

	if (enable) {
		ded_val = ENABLE_VPLL2_DEDICATED;
		grp_val = ENABLE_VPLL2_DEV_GRP;
	} else {
		ded_val = 0;
		grp_val = 0;
	}

	twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
			ded_val, TWL4030_VPLL2_DEDICATED);
	twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER,
			grp_val, TWL4030_VPLL2_DEV_GRP);
}

static int sdp3430_dsi_power_up(void)
{
	if (omap_rev() > OMAP3430_REV_ES1_0)
		enable_vpll2(1);
	return 0;
}

static void sdp3430_dsi_power_down(void)
{
	if (omap_rev() > OMAP3430_REV_ES1_0)
		enable_vpll2(0);
}

static void __init sdp3430_display_init(void)
{
	int r;

	enable_gpio    = SDP3430_LCD_PANEL_ENABLE_GPIO;
	backlight_gpio = SDP3430_LCD_PANEL_BACKLIGHT_GPIO;

	r = gpio_request(enable_gpio, "LCD reset");
	if (r) {
		printk(KERN_ERR "failed to get LCD reset GPIO\n");
		goto err0;
	}

	r = gpio_request(backlight_gpio, "LCD Backlight");
	if (r) {
		printk(KERN_ERR "failed to get LCD backlight GPIO\n");
		goto err1;
	}

	gpio_direction_output(enable_gpio, 0);
	gpio_direction_output(backlight_gpio, 0);

	return;
err1:
	gpio_free(enable_gpio);
err0:
	return;
}

static int sdp3430_panel_enable_lcd(struct omap_display *display)
{
	u8 ded_val, ded_reg;
	u8 grp_val, grp_reg;

	if (dvi_enabled) {
		printk(KERN_ERR "cannot enable LCD, DVI is enabled\n");
		return -EINVAL;
	}

	ded_reg = TWL4030_VAUX3_DEDICATED;
	ded_val = ENABLE_VAUX3_DEDICATED;
	grp_reg = TWL4030_VAUX3_DEV_GRP;
	grp_val = ENABLE_VAUX3_DEV_GRP;

	gpio_direction_output(enable_gpio, 1);
	gpio_direction_output(backlight_gpio, 1);

	if (0 != t2_out(PM_RECEIVER, ded_val, ded_reg))
		return -EIO;
	if (0 != t2_out(PM_RECEIVER, grp_val, grp_reg))
		return -EIO;

	sdp3430_dsi_power_up();

	lcd_enabled = 1;

	return 0;
}

static void sdp3430_panel_disable_lcd(struct omap_display *display)
{
	lcd_enabled = 0;

	sdp3430_dsi_power_down();

	gpio_direction_output(enable_gpio, 0);
	gpio_direction_output(backlight_gpio, 0);
}

static struct omap_dss_display_config sdp3430_display_data = {
	.type = OMAP_DISPLAY_TYPE_DPI,
	.name = "lcd",
	.panel_name = "sharp-ls037v7dw01",
	.u.dpi.data_lines = 16,
	.panel_enable = sdp3430_panel_enable_lcd,
	.panel_disable = sdp3430_panel_disable_lcd,
};

static int sdp3430_panel_enable_dvi(struct omap_display *display)
{
	if (lcd_enabled) {
		printk(KERN_ERR "cannot enable DVI, LCD is enabled\n");
		return -EINVAL;
	}

	sdp3430_dsi_power_up();

	dvi_enabled = 1;

	return 0;
}

static void sdp3430_panel_disable_dvi(struct omap_display *display)
{
	sdp3430_dsi_power_down();

	dvi_enabled = 0;
}


static struct omap_dss_display_config sdp3430_display_data_dvi = {
	.type = OMAP_DISPLAY_TYPE_DPI,
	.name = "dvi",
	.panel_name = "panel-generic",
	.u.dpi.data_lines = 24,
	.panel_enable = sdp3430_panel_enable_dvi,
	.panel_disable = sdp3430_panel_disable_dvi,
};

static int sdp3430_panel_enable_tv(struct omap_display *display)
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

static void sdp3430_panel_disable_tv(struct omap_display *display)
{
	twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, 0x00,
			TWL4030_VDAC_DEDICATED);
	twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, 0x00,
			TWL4030_VDAC_DEV_GRP);
}

static struct omap_dss_display_config sdp3430_display_data_tv = {
	.type = OMAP_DISPLAY_TYPE_VENC,
	.name = "tv",
	.u.venc.type = OMAP_DSS_VENC_TYPE_SVIDEO,
	.panel_enable = sdp3430_panel_enable_tv,
	.panel_disable = sdp3430_panel_disable_tv,
};

static struct omap_dss_board_info sdp3430_dss_data = {
	.dsi_power_up = sdp3430_dsi_power_up,
	.dsi_power_down = sdp3430_dsi_power_down,
	.num_displays = 3,
	.displays = {
		&sdp3430_display_data,
		&sdp3430_display_data_dvi,
		&sdp3430_display_data_tv,
	}
};

static struct platform_device sdp3430_dss_device = {
	.name          = "omapdss",
	.id            = -1,
	.dev            = {
		.platform_data = &sdp3430_dss_data,
	},
};

static struct platform_device *sdp3430_devices[] __initdata = {
	&sdp3430_smc91x_device,
	&sdp3430_dss_device,
};

static inline void __init sdp3430_init_smc91x(void)
{
	int eth_cs;
	unsigned long cs_mem_base;
	int eth_gpio = 0;

	eth_cs = SDP3430_SMC91X_CS;

	if (gpmc_cs_request(eth_cs, SZ_16M, &cs_mem_base) < 0) {
		printk(KERN_ERR "Failed to request GPMC mem for smc91x\n");
		return;
	}

	sdp3430_smc91x_resources[0].start = cs_mem_base + 0x0;
	sdp3430_smc91x_resources[0].end   = cs_mem_base + 0xf;
	udelay(100);

	if (system_rev > OMAP3430_REV_ES1_0)
		eth_gpio = OMAP34XX_ETHR_GPIO_IRQ_SDPV2;
	else
		eth_gpio = OMAP34XX_ETHR_GPIO_IRQ_SDPV1;

	sdp3430_smc91x_resources[1].start = OMAP_GPIO_IRQ(eth_gpio);

	if (omap_request_gpio(eth_gpio) < 0) {
		printk(KERN_ERR "Failed to request GPIO%d for smc91x IRQ\n",
			eth_gpio);
		return;
	}
	omap_set_gpio_direction(eth_gpio, 1);
}

static void __init omap_3430sdp_init_irq(void)
{
	omap2_init_common_hw(hyb18m512160af6_sdrc_params);
	omap_init_irq();
	omap_gpio_init();
	sdp3430_init_smc91x();
}

static struct omap_uart_config sdp3430_uart_config __initdata = {
	.enabled_uarts	= ((1 << 0) | (1 << 1) | (1 << 2)),
};

static struct omap_board_config_kernel sdp3430_config[] __initdata = {
	{ OMAP_TAG_UART,	&sdp3430_uart_config },
};

static int sdp3430_batt_table[] = {
/* 0 C*/
30800, 29500, 28300, 27100,
26000, 24900, 23900, 22900, 22000, 21100, 20300, 19400, 18700, 17900,
17200, 16500, 15900, 15300, 14700, 14100, 13600, 13100, 12600, 12100,
11600, 11200, 10800, 10400, 10000, 9630,   9280,   8950,   8620,   8310,
8020,   7730,   7460,   7200,   6950,   6710,   6470,   6250,   6040,   5830,
5640,   5450,   5260,   5090,   4920,   4760,   4600,   4450,   4310,   4170,
4040,   3910,   3790,   3670,   3550
};

static struct twl4030_bci_platform_data sdp3430_bci_data = {
      .battery_tmp_tbl	= sdp3430_batt_table,
      .tblsize		= ARRAY_SIZE(sdp3430_batt_table),
};

static struct twl4030_gpio_platform_data sdp3430_gpio_data = {
	.gpio_base	= OMAP_MAX_GPIO_LINES,
	.irq_base	= TWL4030_GPIO_IRQ_BASE,
	.irq_end	= TWL4030_GPIO_IRQ_END,
};

static struct twl4030_usb_data sdp3430_usb_data = {
	.usb_mode	= T2_USB_MODE_ULPI,
};

static struct twl4030_madc_platform_data sdp3430_madc_data = {
	.irq_line	= 1,
};

static struct twl4030_platform_data sdp3430_twldata = {
	.irq_base	= TWL4030_IRQ_BASE,
	.irq_end	= TWL4030_IRQ_END,

	/* platform_data for children goes here */
	.bci		= &sdp3430_bci_data,
	.gpio		= &sdp3430_gpio_data,
	.madc		= &sdp3430_madc_data,
	.keypad		= &sdp3430_kp_data,
	.usb		= &sdp3430_usb_data,
};

static struct i2c_board_info __initdata sdp3430_i2c_boardinfo[] = {
	{
		I2C_BOARD_INFO("twl4030", 0x48),
		.flags = I2C_CLIENT_WAKE,
		.irq = INT_34XX_SYS_NIRQ,
		.platform_data = &sdp3430_twldata,
	},
};

static int __init omap3430_i2c_init(void)
{
	omap_register_i2c_bus(1, 2600, sdp3430_i2c_boardinfo,
			ARRAY_SIZE(sdp3430_i2c_boardinfo));
	omap_register_i2c_bus(2, 400, NULL, 0);
	omap_register_i2c_bus(3, 400, NULL, 0);
	return 0;
}

extern void __init sdp3430_flash_init(void);

static void __init omap_3430sdp_init(void)
{
	omap3430_i2c_init();
	platform_add_devices(sdp3430_devices, ARRAY_SIZE(sdp3430_devices));
	omap_board_config = sdp3430_config;
	omap_board_config_size = ARRAY_SIZE(sdp3430_config);
	if (system_rev > OMAP3430_REV_ES1_0)
		ts_gpio = OMAP34XX_TS_GPIO_IRQ_SDPV2;
	else
		ts_gpio = OMAP34XX_TS_GPIO_IRQ_SDPV1;
	sdp3430_spi_board_info[0].irq = OMAP_GPIO_IRQ(ts_gpio);
	spi_register_board_info(sdp3430_spi_board_info,
				ARRAY_SIZE(sdp3430_spi_board_info));
	ads7846_dev_init();
	sdp3430_flash_init();
	msecure_init();
	omap_serial_init();
	usb_musb_init();
	usb_ehci_init();
	hsmmc_init();
	sdp3430_display_init();
}

static void __init omap_3430sdp_map_io(void)
{
	omap2_set_globals_343x();
	omap2_map_common_io();
}

MACHINE_START(OMAP_3430SDP, "OMAP3430 3430SDP board")
	/* Maintainer: Syed Khasim - Texas Instruments Inc */
	.phys_io	= 0x48000000,
	.io_pg_offst	= ((0xd8000000) >> 18) & 0xfffc,
	.boot_params	= 0x80000100,
	.map_io		= omap_3430sdp_map_io,
	.init_irq	= omap_3430sdp_init_irq,
	.init_machine	= omap_3430sdp_init,
	.timer		= &omap_timer,
MACHINE_END
