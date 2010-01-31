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
#include <linux/input/matrix_keypad.h>

#include <mach/hardware.h>
#include <mach/gpio.h>

/* hardware debounce, (value + 1) * 31us */
#define GPIO_DEBOUNCE_TIME 0x7f

/* HACK: this requires patched twl4030_keypad driver */
#define FNKEY(row, col, code) KEY((row + 8), col, code)

static int omap3pandora_keymap[] = {
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
	FNKEY(0, 4, KEY_DOLLAR),
	FNKEY(0, 5, KEY_EURO),
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
	.keymap		= omap3pandora_keymap,
	.keymap_size	= ARRAY_SIZE(omap3pandora_keymap),
};

struct twl4030_keypad_data omap3pandora_kp_data = {
	.keymap_data	= &board_map_data,
	.rows		= 8,
	.cols		= 6,
	.rep		= 1,
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
		.code			= KEY_KP2,
		.gpio			= 111,
		.active_low		= 1,
		.desc			= "game 2",
	}, {
		.code			= KEY_KP3,
		.gpio			= 106,
		.active_low		= 1,
		.desc			= "game 3",
	}, {
		.code			= KEY_KP1,
		.gpio			= 109,
		.active_low		= 1,
		.desc			= "game 1",
	}, {
		.code			= KEY_KP4,
		.gpio			= 101,
		.active_low		= 1,
		.desc			= "game 4",
	}, {
		.code			= KEY_KP5,
		.gpio			= 102,
		.active_low		= 1,
		.desc			= "shoulder l",
	}, {
		.code			= KEY_KP7,
		.gpio			= 97,
		.active_low		= 1,
		.desc			= "shoulder l2",
	}, {
		.code			= KEY_KP6,
		.gpio			= 105,
		.active_low		= 1,
		.desc			= "shoulder r",
	}, {
		.code			= KEY_KP8,
		.gpio			= 107,
		.active_low		= 1,
		.desc			= "shoulder r2",
	}, {
		.code			= KEY_LEFTALT,
		.gpio			= 100,
		.active_low		= 0,
		.desc			= "start",
	}, {
		.code			= KEY_LEFTCTRL,
		.gpio			= 104,
		.active_low		= 1,
		.desc			= "select",
	}, {
		.code			= KEY_MENU,
		.gpio			= 99,
		.active_low		= 1,
		.desc			= "menu",
	}, {
		.code			= KEY_COFFEE,
		.gpio			= 176,
		.active_low		= 1,
		.desc			= "hold",
	}, {
		.type			= EV_SW,
		.code			= SW_LID,
		.gpio			= 108,
		.active_low		= 1,
		.desc			= "lid switch",
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
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(gpio_buttons); i++)
		omap_set_gpio_debounce(gpio_buttons[i].gpio, 1);

	/* set debounce time for banks 4 and 6 */
	omap_set_gpio_debounce_time(32 * 3, GPIO_DEBOUNCE_TIME);
	omap_set_gpio_debounce_time(32 * 5, GPIO_DEBOUNCE_TIME);

	ret = platform_device_register(&omap3pandora_keys_gpio);
	if (ret != 0)
		printk(KERN_ERR "Failed to register gpio-keys\n");
}

