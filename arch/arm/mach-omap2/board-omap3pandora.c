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

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

#include <linux/spi/spi.h>
#include <linux/regulator/machine.h>
#include <linux/i2c/twl.h>
#include <linux/wl12xx.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/nand.h>
#include <linux/leds.h>
#include <linux/leds_pwm.h>
#include <linux/input.h>
#include <linux/input/matrix_keypad.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/regulator/fixed.h>
#include <linux/i2c/vsense.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <plat/board.h>
#include <plat/common.h>
#include <mach/hardware.h>
#include <plat/mcspi.h>
#include <plat/usb.h>
#include <video/omapdss.h>
#include <plat/nand.h>

#include "mux.h"
#include "sdram-micron-mt46h32m32lf-6.h"
#include "hsmmc.h"
#include "common-board-devices.h"

#define PANDORA_WIFI_IRQ_GPIO		21
#define PANDORA_WIFI_NRESET_GPIO	23
#define OMAP3_PANDORA_TS_GPIO		94
#define PANDORA_EN_USB_5V_GPIO		164

static struct mtd_partition omap3pandora_nand_partitions[] = {
	{
		.name           = "xloader",
		.offset         = 0,
		.size           = 4 * NAND_BLOCK_SIZE,
		.mask_flags     = MTD_WRITEABLE
	}, {
		.name           = "uboot",
		.offset         = MTDPART_OFS_APPEND,
		.size           = 15 * NAND_BLOCK_SIZE,
	}, {
		.name           = "uboot-env",
		.offset         = MTDPART_OFS_APPEND,
		.size           = 1 * NAND_BLOCK_SIZE,
	}, {
		.name           = "boot",
		.offset         = MTDPART_OFS_APPEND,
		.size           = 80 * NAND_BLOCK_SIZE,
	}, {
		.name           = "rootfs",
		.offset         = MTDPART_OFS_APPEND,
		.size           = MTDPART_SIZ_FULL,
	},
};

static struct omap_nand_platform_data pandora_nand_data = {
	.cs		= 0,
	.devsize	= NAND_BUSWIDTH_16,
	.xfer_type	= NAND_OMAP_PREFETCH_DMA,
	.parts		= omap3pandora_nand_partitions,
	.nr_parts	= ARRAY_SIZE(omap3pandora_nand_partitions),
};

static struct gpio_led pandora_gpio_leds[] = {
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
		.gpio			= 158,
	}, {
		.name			= "pandora::wifi",
		.gpio			= 159,
	},
};

static struct gpio_led_platform_data pandora_gpio_led_data = {
	.leds		= pandora_gpio_leds,
	.num_leds	= ARRAY_SIZE(pandora_gpio_leds),
};

static struct platform_device pandora_leds_gpio = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &pandora_gpio_led_data,
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
		.default_trigger	= "bq27500-0-charging",
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

static struct platform_device pandora_bl = {
	.name	= "pandora-backlight",
	.id	= -1,
};

#define GPIO_BUTTON(gpio_num, ev_type, ev_code, act_low, descr)	\
{								\
	.gpio		= gpio_num,				\
	.type		= ev_type,				\
	.code		= ev_code,				\
	.active_low	= act_low,				\
	.debounce_interval = 4,					\
	.desc		= "btn " descr,				\
}

#define GPIO_BUTTON_LOW(gpio_num, event_code, description)	\
	GPIO_BUTTON(gpio_num, EV_KEY, event_code, 1, description)

static struct gpio_keys_button pandora_gpio_keys[] = {
	GPIO_BUTTON_LOW(110,	KEY_UP,		"up"),
	GPIO_BUTTON_LOW(103,	KEY_DOWN,	"down"),
	GPIO_BUTTON_LOW(96,	KEY_LEFT,	"left"),
	GPIO_BUTTON_LOW(98,	KEY_RIGHT,	"right"),
	GPIO_BUTTON_LOW(109,	KEY_PAGEUP,	"game 1"),
	GPIO_BUTTON_LOW(111,	KEY_END,	"game 2"),
	GPIO_BUTTON_LOW(106,	KEY_PAGEDOWN,	"game 3"),
	GPIO_BUTTON_LOW(101,	KEY_HOME,	"game 4"),
	GPIO_BUTTON_LOW(102,	KEY_RIGHTSHIFT,	"l"),
	GPIO_BUTTON_LOW(97,	KEY_KPPLUS,	"l2"),
	GPIO_BUTTON_LOW(105,	KEY_RIGHTCTRL,	"r"),
	GPIO_BUTTON_LOW(107,	KEY_KPMINUS,	"r2"),
	GPIO_BUTTON_LOW(104,	KEY_LEFTCTRL,	"ctrl"),
	GPIO_BUTTON_LOW(99,	KEY_MENU,	"menu"),
	GPIO_BUTTON_LOW(176,	KEY_COFFEE,	"hold"),
	GPIO_BUTTON(100, EV_KEY, KEY_LEFTALT, 0, "alt"),
	GPIO_BUTTON(108, EV_SW, SW_LID, 1, "lid"),
};

static struct gpio_keys_platform_data pandora_gpio_key_info = {
	.buttons	= pandora_gpio_keys,
	.nbuttons	= ARRAY_SIZE(pandora_gpio_keys),
};

static struct platform_device pandora_keys_gpio = {
	.name	= "gpio-keys",
	.id	= -1,
	.dev	= {
		.platform_data	= &pandora_gpio_key_info,
	},
};

/* HACK: this requires patched twl4030_keypad driver */
#define FNKEY(row, col, code) KEY((row + 8), col, code)

static const uint32_t board_keymap[] = {
	/* row, col, code */
	KEY(0, 0, KEY_9),
	KEY(0, 1, KEY_8),
	KEY(0, 2, KEY_I),
	KEY(0, 3, KEY_J),
	KEY(0, 4, KEY_N),
	KEY(0, 5, KEY_M),
	KEY(1, 0, KEY_0),
	KEY(1, 1, KEY_7),
	KEY(1, 2, KEY_U),
	KEY(1, 3, KEY_H),
	KEY(1, 4, KEY_B),
	KEY(1, 5, KEY_SPACE),
	KEY(2, 0, KEY_BACKSPACE),
	KEY(2, 1, KEY_6),
	KEY(2, 2, KEY_Y),
	KEY(2, 3, KEY_G),
	KEY(2, 4, KEY_V),
	KEY(2, 5, KEY_FN),
	KEY(3, 0, KEY_O),
	KEY(3, 1, KEY_5),
	KEY(3, 2, KEY_T),
	KEY(3, 3, KEY_F),
	KEY(3, 4, KEY_C),
	KEY(4, 0, KEY_P),
	KEY(4, 1, KEY_4),
	KEY(4, 2, KEY_R),
	KEY(4, 3, KEY_D),
	KEY(4, 4, KEY_X),
	KEY(5, 0, KEY_K),
	KEY(5, 1, KEY_3),
	KEY(5, 2, KEY_E),
	KEY(5, 3, KEY_S),
	KEY(5, 4, KEY_Z),
	KEY(6, 0, KEY_L),
	KEY(6, 1, KEY_2),
	KEY(6, 2, KEY_W),
	KEY(6, 3, KEY_A),
	KEY(6, 4, KEY_DOT),
	KEY(7, 0, KEY_ENTER),
	KEY(7, 1, KEY_1),
	KEY(7, 2, KEY_Q),
	KEY(7, 3, KEY_LEFTSHIFT),
	KEY(7, 4, KEY_COMMA),
	/* Fn keys */
	FNKEY(0, 0, KEY_F9),
	FNKEY(0, 1, KEY_F8),
	FNKEY(0, 2, KEY_BRIGHTNESSUP),
	FNKEY(0, 3, KEY_F13),		/* apostrophe, differs from Fn-A? */
	FNKEY(0, 4, KEY_F22),
	FNKEY(0, 5, KEY_F23),
	FNKEY(1, 0, KEY_F10),
	FNKEY(1, 1, KEY_F7),
	FNKEY(1, 2, KEY_BRIGHTNESSDOWN),
	FNKEY(1, 3, KEY_GRAVE),
	FNKEY(1, 4, KEY_F14),		/* pipe/bar */
	FNKEY(1, 5, KEY_TAB),
	FNKEY(2, 0, KEY_INSERT),
	FNKEY(2, 1, KEY_F6),
	FNKEY(2, 2, KEY_F15),		/* dash */
	FNKEY(2, 3, KEY_EQUAL),
	FNKEY(2, 4, KEY_F16),		/* # (pound/hash) */
	FNKEY(2, 5, KEY_FN),
	FNKEY(3, 0, KEY_F11),
	FNKEY(3, 1, KEY_F5),
	FNKEY(3, 2, KEY_F17),		/* ! */
	FNKEY(3, 3, KEY_KPPLUS),
	FNKEY(3, 4, KEY_BACKSLASH),
	FNKEY(4, 0, KEY_F12),
	FNKEY(4, 1, KEY_F4),
	FNKEY(4, 2, KEY_RIGHTBRACE),
	FNKEY(4, 3, KEY_KPMINUS),
	FNKEY(4, 4, KEY_QUESTION),
	FNKEY(5, 0, KEY_F18),		/* £ (pound) */
	FNKEY(5, 1, KEY_F3),
	FNKEY(5, 2, KEY_LEFTBRACE),
	FNKEY(5, 3, KEY_F19),		/* " */
	FNKEY(5, 4, KEY_SLASH),
	FNKEY(6, 0, KEY_YEN),
	FNKEY(6, 1, KEY_F2),
	FNKEY(6, 2, KEY_F20),		/* @ */
	FNKEY(6, 3, KEY_APOSTROPHE),
	FNKEY(6, 4, KEY_F21),		/* : */
	FNKEY(7, 0, KEY_ENTER),
	FNKEY(7, 1, KEY_F1),
	FNKEY(7, 2, KEY_ESC),
	FNKEY(7, 3, KEY_CAPSLOCK),
	FNKEY(7, 4, KEY_SEMICOLON),
};

static struct matrix_keymap_data board_map_data = {
	.keymap			= board_keymap,
	.keymap_size		= ARRAY_SIZE(board_keymap),
};

static struct twl4030_keypad_data pandora_kp_data = {
	.keymap_data	= &board_map_data,
	.rows		= 8,
	.cols		= 6,
	.rep		= 1,
};

static struct omap_dss_device pandora_lcd_device = {
	.name			= "lcd",
	.driver_name		= "tpo_td043mtea1_panel",
	.type			= OMAP_DISPLAY_TYPE_DPI,
	.phy.dpi.data_lines	= 24,
	.reset_gpio		= 157,
};

static struct omap_dss_device pandora_tv_device = {
	.name			= "tv",
	.driver_name		= "venc",
	.type			= OMAP_DISPLAY_TYPE_VENC,
	.phy.venc.type		= OMAP_DSS_VENC_TYPE_SVIDEO,
};

static struct omap_dss_device *pandora_dss_devices[] = {
	&pandora_lcd_device,
	&pandora_tv_device,
};

static struct omap_dss_board_info pandora_dss_data = {
	.num_devices	= ARRAY_SIZE(pandora_dss_devices),
	.devices	= pandora_dss_devices,
	.default_device	= &pandora_lcd_device,
};

static void pandora_wl1251_init_card(struct mmc_card *card)
{
	/*
	 * We have TI wl1251 attached to MMC3. Pass this information to
	 * SDIO core because it can't be probed by normal methods.
	 */
	card->quirks |= MMC_QUIRK_NONSTD_SDIO;
	card->cccr.wide_bus = 1;
	card->cis.vendor = 0x104c;
	card->cis.device = 0x9066;
	card->cis.blksize = 512;
	card->cis.max_dtr = 20000000;
}

static struct omap2_hsmmc_info omap3pandora_mmc[] = {
	{
		.mmc		= 1,
		.caps		= MMC_CAP_4_BIT_DATA,
		.gpio_cd	= -EINVAL,
		.gpio_wp	= 126,
		.ext_clock	= 0,
	},
	{
		.mmc		= 2,
		.caps		= MMC_CAP_4_BIT_DATA,
		.gpio_cd	= -EINVAL,
		.gpio_wp	= 127,
		.ext_clock	= 1,
		.transceiver	= true,
	},
	{
		.mmc		= 3,
		.caps		= MMC_CAP_4_BIT_DATA | MMC_CAP_POWER_OFF_CARD,
		.gpio_cd	= -EINVAL,
		.gpio_wp	= -EINVAL,
		.init_card	= pandora_wl1251_init_card,
	},
	{}	/* Terminator */
};

static int omap3pandora_twl_gpio_setup(struct device *dev,
		unsigned gpio, unsigned ngpio)
{
	int ret, gpio_32khz;

	/* gpio + {0,1} is "mmc{0,1}_cd" (input/IRQ) */
	omap3pandora_mmc[0].gpio_cd = gpio + 0;
	omap3pandora_mmc[1].gpio_cd = gpio + 1;
	omap2_hsmmc_init(omap3pandora_mmc);

	/* gpio + 13 drives 32kHz buffer for wifi module */
	gpio_32khz = gpio + 13;
	ret = gpio_request_one(gpio_32khz, GPIOF_OUT_INIT_HIGH, "wifi 32kHz");
	if (ret < 0) {
		pr_err("Cannot get GPIO line %d, ret=%d\n", gpio_32khz, ret);
		return -ENODEV;
	}

	return 0;
}

static struct twl4030_gpio_platform_data omap3pandora_gpio_data = {
	.gpio_base	= OMAP_MAX_GPIO_LINES,
	.irq_base	= TWL4030_GPIO_IRQ_BASE,
	.irq_end	= TWL4030_GPIO_IRQ_END,
	.setup		= omap3pandora_twl_gpio_setup,
};

static struct regulator_consumer_supply pandora_vmmc1_supply[] = {
	REGULATOR_SUPPLY("vmmc", "omap_hsmmc.0"),
};

static struct regulator_consumer_supply pandora_vmmc2_supply[] = {
	REGULATOR_SUPPLY("vmmc", "omap_hsmmc.1")
};

static struct regulator_consumer_supply pandora_vmmc3_supply[] = {
	REGULATOR_SUPPLY("vmmc", "omap_hsmmc.2"),
};

static struct regulator_consumer_supply pandora_vdds_supplies[] = {
	REGULATOR_SUPPLY("vdds_sdi", "omapdss"),
	REGULATOR_SUPPLY("vdds_dsi", "omapdss"),
	REGULATOR_SUPPLY("vdds_dsi", "omapdss_dsi.0"),
};

static struct regulator_consumer_supply pandora_vcc_lcd_supply[] = {
	REGULATOR_SUPPLY("vcc", "display0"),
};

static struct regulator_consumer_supply pandora_usb_phy_supply[] = {
	REGULATOR_SUPPLY("hsusb0", "ehci-omap.0"),
};

/* ads7846 on SPI and 2 nub controllers on I2C */
static struct regulator_consumer_supply pandora_vaux4_supplies[] = {
	REGULATOR_SUPPLY("vcc", "spi1.0"),
	REGULATOR_SUPPLY("vcc", "3-0066"),
	REGULATOR_SUPPLY("vcc", "3-0067"),
};

static struct regulator_consumer_supply pandora_adac_supply[] = {
	REGULATOR_SUPPLY("vcc", "soc-audio"),
	REGULATOR_SUPPLY("lidsw", NULL),
};

/* VMMC1 for MMC1 pins CMD, CLK, DAT0..DAT3 (20 mA, plus card == max 220 mA) */
static struct regulator_init_data pandora_vmmc1 = {
	.constraints = {
		.min_uV			= 1850000,
		.max_uV			= 3150000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(pandora_vmmc1_supply),
	.consumer_supplies	= pandora_vmmc1_supply,
};

/* VMMC2 for MMC2 pins CMD, CLK, DAT0..DAT3 (max 100 mA) */
static struct regulator_init_data pandora_vmmc2 = {
	.constraints = {
		.min_uV			= 1850000,
		.max_uV			= 3150000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(pandora_vmmc2_supply),
	.consumer_supplies	= pandora_vmmc2_supply,
};

/* VAUX1 for LCD */
static struct regulator_init_data pandora_vaux1 = {
	.constraints = {
		.min_uV			= 3000000,
		.max_uV			= 3000000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(pandora_vcc_lcd_supply),
	.consumer_supplies	= pandora_vcc_lcd_supply,
};

/* VAUX2 for USB host PHY */
static struct regulator_init_data pandora_vaux2 = {
	.constraints = {
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(pandora_usb_phy_supply),
	.consumer_supplies	= pandora_usb_phy_supply,
};

/* VAUX4 for ads7846 and nubs */
static struct regulator_init_data pandora_vaux4 = {
	.constraints = {
		.min_uV			= 2800000,
		.max_uV			= 2800000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(pandora_vaux4_supplies),
	.consumer_supplies	= pandora_vaux4_supplies,
};

/* VSIM for audio DAC */
static struct regulator_init_data pandora_vsim = {
	.constraints = {
		.min_uV			= 2800000,
		.max_uV			= 2800000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(pandora_adac_supply),
	.consumer_supplies	= pandora_adac_supply,
};

/* Fixed regulator internal to Wifi module */
static struct regulator_init_data pandora_vmmc3 = {
	.constraints = {
		.valid_ops_mask		= REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(pandora_vmmc3_supply),
	.consumer_supplies	= pandora_vmmc3_supply,
};

static struct fixed_voltage_config pandora_vwlan = {
	.supply_name		= "vwlan",
	.microvolts		= 1800000, /* 1.8V */
	.gpio			= PANDORA_WIFI_NRESET_GPIO,
	.startup_delay		= 50000, /* 50ms */
	.enable_high		= 1,
	.enabled_at_boot	= 0,
	.init_data		= &pandora_vmmc3,
};

static struct platform_device pandora_vwlan_device = {
	.name		= "reg-fixed-voltage",
	.id		= 1,
	.dev = {
		.platform_data = &pandora_vwlan,
	},
};

static char *pandora_power_supplied_to[] = {
	"bq27500-0",
};

static struct twl4030_bci_platform_data pandora_bci_data = {
	.supplied_to		= pandora_power_supplied_to,
	.num_supplicants	= ARRAY_SIZE(pandora_power_supplied_to),
};

static struct twl4030_power_data pandora_power_data = {
	.use_poweroff	= 1,
};

static struct twl4030_platform_data omap3pandora_twldata = {
	.gpio		= &omap3pandora_gpio_data,
	.vmmc1		= &pandora_vmmc1,
	.vmmc2		= &pandora_vmmc2,
	.vaux1		= &pandora_vaux1,
	.vaux2		= &pandora_vaux2,
	.vaux4		= &pandora_vaux4,
	.vsim		= &pandora_vsim,
	.keypad		= &pandora_kp_data,
	.bci		= &pandora_bci_data,
	.power		= &pandora_power_data,
};

static struct i2c_board_info __initdata omap3pandora_i2c_boardinfo[] = {
	{
		I2C_BOARD_INFO("tps65950", 0x48),
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
	omap3_pmic_get_config(&omap3pandora_twldata,
			TWL_COMMON_PDATA_USB | TWL_COMMON_PDATA_AUDIO,
			TWL_COMMON_REGULATOR_VDAC | TWL_COMMON_REGULATOR_VPLL2);

	omap3pandora_twldata.vdac->constraints.apply_uV = true;

	omap3pandora_twldata.vpll2->constraints.apply_uV = true;
	omap3pandora_twldata.vpll2->num_consumer_supplies =
					ARRAY_SIZE(pandora_vdds_supplies);
	omap3pandora_twldata.vpll2->consumer_supplies = pandora_vdds_supplies;

	omap3_pmic_init("tps65950", &omap3pandora_twldata);
	/* i2c2 pins are not connected */
	omap_register_i2c_bus(3, 100, omap3pandora_i2c3_boardinfo,
			ARRAY_SIZE(omap3pandora_i2c3_boardinfo));
	return 0;
}

static struct spi_board_info omap3pandora_spi_board_info[] __initdata = {
	{
		.modalias		= "tpo_td043mtea1_panel_spi",
		.bus_num		= 1,
		.chip_select		= 1,
		.max_speed_hz		= 375000,
		.platform_data		= &pandora_lcd_device,
	}
};

static void __init pandora_wl1251_init(void)
{
	struct wl12xx_platform_data pandora_wl1251_pdata;
	int ret;

	memset(&pandora_wl1251_pdata, 0, sizeof(pandora_wl1251_pdata));

	ret = gpio_request_one(PANDORA_WIFI_IRQ_GPIO, GPIOF_IN, "wl1251 irq");
	if (ret < 0)
		goto fail;

	pandora_wl1251_pdata.irq = gpio_to_irq(PANDORA_WIFI_IRQ_GPIO);
	if (pandora_wl1251_pdata.irq < 0)
		goto fail_irq;

	pandora_wl1251_pdata.use_eeprom = true;
	ret = wl12xx_set_platform_data(&pandora_wl1251_pdata);
	if (ret < 0)
		goto fail_irq;

	return;

fail_irq:
	gpio_free(PANDORA_WIFI_IRQ_GPIO);
fail:
	printk(KERN_ERR "wl1251 board initialisation failed\n");
}

static void __init pandora_usb_host_init(void)
{
	int ret;

	ret = gpio_request_one(PANDORA_EN_USB_5V_GPIO, GPIOF_OUT_INIT_HIGH,
		"ehci vbus");
	if (ret < 0)
		pr_err("Cannot set vbus GPIO, ret=%d\n", ret);
}

static struct platform_device *omap3pandora_devices[] __initdata = {
	&pandora_leds_gpio,
	&pandora_leds_pwm,
	&pandora_bl,
	&pandora_keys_gpio,
	&pandora_vwlan_device,
};

static const struct usbhs_omap_board_data usbhs_bdata __initconst = {

	.port_mode[0] = OMAP_EHCI_PORT_MODE_PHY,
	.port_mode[1] = OMAP_USBHS_PORT_MODE_UNUSED,
	.port_mode[2] = OMAP_USBHS_PORT_MODE_UNUSED,

	.phy_reset  = true,
	.reset_gpio_port[0]  = 16,
	.reset_gpio_port[1]  = -EINVAL,
	.reset_gpio_port[2]  = -EINVAL
};

#ifdef CONFIG_OMAP_MUX
static struct omap_board_mux board_mux[] __initdata = {
	{ .reg_offset = OMAP_MUX_TERMINATOR },
};
#endif

static struct regulator *lid_switch_power;

#ifdef CONFIG_PM_SLEEP
static int pandora_pm_suspend(struct device *dev)
{
	if (!IS_ERR_OR_NULL(lid_switch_power))
		regulator_disable(lid_switch_power);

	return 0;
}

static int pandora_pm_resume(struct device *dev)
{
	if (!IS_ERR_OR_NULL(lid_switch_power))
		regulator_enable(lid_switch_power);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(pandora_pm, pandora_pm_suspend, pandora_pm_resume);

static int __devinit pandora_pm_probe(struct platform_device *pdev)
{
	lid_switch_power = regulator_get(NULL, "lidsw");
	if (!IS_ERR(lid_switch_power))
		regulator_enable(lid_switch_power);

	return 0;
}

static struct platform_driver pandora_pm_driver = {
	.probe		= pandora_pm_probe,
	.driver		= {
		.name	= "pandora-pm",
		.pm	= &pandora_pm,
	},
};

static struct platform_device pandora_pm_dev = {
	.name	= "pandora-pm",
	.id	= -1,
};

static int __init pandora_pm_drv_reg(void)
{
	platform_device_register(&pandora_pm_dev);
	return platform_driver_register(&pandora_pm_driver);
}
late_initcall(pandora_pm_drv_reg);

static void __init omap3pandora_init(void)
{
	omap3_mux_init(board_mux, OMAP_PACKAGE_CBB);
	pandora_usb_host_init();
	omap3pandora_i2c_init();
	pandora_wl1251_init();
	platform_add_devices(omap3pandora_devices,
			ARRAY_SIZE(omap3pandora_devices));
	omap_display_init(&pandora_dss_data);
	omap_serial_init();
	omap_sdrc_init(mt46h32m32lf6_sdrc_params,
				  mt46h32m32lf6_sdrc_params);
	spi_register_board_info(omap3pandora_spi_board_info,
			ARRAY_SIZE(omap3pandora_spi_board_info));
	omap_ads7846_init(1, OMAP3_PANDORA_TS_GPIO, 0, NULL);
	usbhs_init(&usbhs_bdata);
	usb_musb_init(NULL);
	gpmc_nand_init(&pandora_nand_data);

	/* Ensure SDRC pins are mux'd for self-refresh */
	omap_mux_init_signal("sdrc_cke0", OMAP_PIN_OUTPUT);
	omap_mux_init_signal("sdrc_cke1", OMAP_PIN_OUTPUT);
}

/* HACK: create it here, so that others don't need to bother */
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>

static int __init proc_pandora_init(void)
{
	struct proc_dir_entry *ret;

	ret = proc_mkdir("pandora", NULL);
	if (!ret)
		return -ENOMEM;
	return 0;
}
fs_initcall(proc_pandora_init);
#endif

MACHINE_START(OMAP3_PANDORA, "Pandora Handheld Console")
	.atag_offset	= 0x100,
	.reserve	= omap_reserve,
	.map_io		= omap3_map_io,
	.init_early	= omap35xx_init_early,
	.init_irq	= omap3_init_irq,
	.init_machine	= omap3pandora_init,
	.timer		= &omap3_timer,
MACHINE_END
