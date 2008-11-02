/*
 * board-omap3pandora-input.c
 *
 * Input mapping for Pandora handheld console
 * Author: Grazvydas Ignotas <notasas@gmail.com>
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
#include <linux/input.h>
#include <linux/gpio_keys.h>

#include <mach/hardware.h>
#include <mach/keypad.h>

static int omap3pandora_keymap[] = {
	/* col, row, code */
	KEY(0, 0, KEY_9),
	KEY(0, 1, KEY_0),
	KEY(0, 2, KEY_BACKSPACE),
	KEY(0, 3, KEY_O),
	KEY(0, 4, KEY_P),
	KEY(0, 5, KEY_K),
	KEY(0, 6, KEY_L),
	KEY(0, 7, KEY_ENTER),
	KEY(1, 0, KEY_8),
	KEY(1, 1, KEY_7),
	KEY(1, 2, KEY_6),
	KEY(1, 3, KEY_5),
	KEY(1, 4, KEY_4),
	KEY(1, 5, KEY_3),
	KEY(1, 6, KEY_2),
	KEY(1, 7, KEY_1),
	KEY(2, 0, KEY_I),
	KEY(2, 1, KEY_U),
	KEY(2, 2, KEY_Y),
	KEY(2, 3, KEY_T),
	KEY(2, 4, KEY_R),
	KEY(2, 5, KEY_E),
	KEY(2, 6, KEY_W),
	KEY(2, 7, KEY_Q),
	KEY(3, 0, KEY_J),
	KEY(3, 1, KEY_H),
	KEY(3, 2, KEY_G),
	KEY(3, 3, KEY_F),
	KEY(3, 4, KEY_D),
	KEY(3, 5, KEY_S),
	KEY(3, 6, KEY_A),
	KEY(3, 7, KEY_LEFTSHIFT),
	KEY(4, 0, KEY_N),
	KEY(4, 1, KEY_B),
	KEY(4, 2, KEY_V),
	KEY(4, 3, KEY_C),
	KEY(4, 4, KEY_X),
	KEY(4, 5, KEY_Z),
	KEY(4, 6, KEY_DOT),
	KEY(4, 7, KEY_COMMA),
	KEY(5, 0, KEY_M),
	KEY(5, 1, KEY_SPACE),
	KEY(5, 2, KEY_SPACE),
	KEY(5, 3, KEY_FN),
};

struct twl4030_keypad_data omap3pandora_kp_data = {
	.rows		= 8,
	.cols		= 6,
	.keymap		= omap3pandora_keymap,
	.keymapsize	= ARRAY_SIZE(omap3pandora_keymap),
	.rep		= 1,
	.irq		= TWL4030_MODIRQ_KEYPAD,
};

static struct gpio_keys_button gpio_buttons[] = {
	{
		.code			= KEY_UP,
		.gpio			= 110,
		.active_low		= 1,
		.desc			= "dpad up",
	}, {
		.code			= KEY_DOWN,
		.gpio			= 103,
		.active_low		= 1,
		.desc			= "dpad down",
	}, {
		.code			= KEY_LEFT,
		.gpio			= 96,
		.active_low		= 1,
		.desc			= "dpad left",
	}, {
		.code			= KEY_RIGHT,
		.gpio			= 98,
		.active_low		= 1,
		.desc			= "dpad right",
	}, {
		.code			= BTN_A,
		.gpio			= 111,
		.active_low		= 1,
		.desc			= "a",
	}, {
		.code			= BTN_B,
		.gpio			= 106,
		.active_low		= 1,
		.desc			= "b",
	}, {
		.code			= BTN_X,
		.gpio			= 109,
		.active_low		= 1,
		.desc			= "x",
	}, {
		.code			= BTN_Y,
		.gpio			= 101,
		.active_low		= 1,
		.desc			= "y",
	}, {
		.code			= BTN_TL,
		.gpio			= 102,
		.active_low		= 1,
		.desc			= "shoulder l",
	}, {
		.code			= BTN_TL2,
		.gpio			= 97,
		.active_low		= 1,
		.desc			= "shoulder l2",
	}, {
		.code			= BTN_TR,
		.gpio			= 105,
		.active_low		= 1,
		.desc			= "shoulder r",
	}, {
		.code			= BTN_TR2,
		.gpio			= 107,
		.active_low		= 1,
		.desc			= "shoulder r2",
	}, {
		.code			= BTN_START,
		.gpio			= 100,
		.active_low		= 1,
		.desc			= "start",
	}, {
		.code			= BTN_SELECT,
		.gpio			= 104,
		.active_low		= 1,
		.desc			= "select",
	}, {
		.code			= KEY_MENU,
		.gpio			= 99,
		.active_low		= 1,
		.desc			= "menu",
	},
};

static struct gpio_keys_platform_data gpio_key_info = {
	.buttons	= gpio_buttons,
	.nbuttons	= ARRAY_SIZE(gpio_buttons),
};

static struct platform_device omap3pandora_keys_gpio = {
	.name	= "gpio-keys",
	.id	= -1,
	.dev	= {
		.platform_data	= &gpio_key_info,
	},
};

void __init omap3pandora_input_init(void)
{
	int ret;

	ret = platform_device_register(&omap3pandora_keys_gpio);
	if (ret != 0)
	{
		printk(KERN_ERR "Failed to register gpio-keys\n");
	}
}

