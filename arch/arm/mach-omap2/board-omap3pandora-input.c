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
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

#include <mach/hardware.h>
#include <mach/gpio.h>

#define GPIO_KEYS_PROC "pandora/game_button_mode"

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
	.keymap		= omap3pandora_keymap,
	.keymap_size	= ARRAY_SIZE(omap3pandora_keymap),
};

struct twl4030_keypad_data omap3pandora_kp_data = {
	.keymap_data	= &board_map_data,
	.rows		= 8,
	.cols		= 6,
	.rep		= 1,
};

#define GPIO_BUTTON(gpio_num, ev_type, ev_code, act_low, descr)	\
{								\
	.gpio		= gpio_num,				\
	.type		= ev_type,				\
	.code		= ev_code,				\
	.active_low	= act_low,				\
	.desc		= "btn " descr,				\
}

#define GPIO_BUTTON_LOW(gpio_num, event_code, description)	\
	GPIO_BUTTON(gpio_num, EV_KEY, event_code, 1, description)

static struct gpio_keys_button gpio_buttons[] = {
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
	GPIO_BUTTON(100, EV_KEY, KEY_LEFTALT, 0, "alt"),
	GPIO_BUTTON_LOW(99,	KEY_MENU,	"menu"),
	GPIO_BUTTON_LOW(176,	KEY_COFFEE,	"hold"),
	GPIO_BUTTON(108, EV_SW, SW_LID, 1, "lid"),
};

static const unsigned short buttons_kbd[] = {
	KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
	KEY_PAGEUP, KEY_END, KEY_PAGEDOWN, KEY_HOME,
	KEY_RIGHTSHIFT, KEY_KPPLUS, KEY_RIGHTCTRL, KEY_KPMINUS,
	KEY_LEFTCTRL, KEY_LEFTALT,
};

static const unsigned short buttons_joy[] = {
	BTN_0, BTN_1, BTN_2, BTN_3,
	BTN_BASE, BTN_BASE2, BTN_BASE3, BTN_BASE4,
	BTN_TL, BTN_TL2, BTN_TR, BTN_TR2,
	BTN_SELECT, BTN_START,
};

static struct gpio_keys_platform_data gpio_key_info = {
	.buttons	= gpio_buttons,
	.nbuttons	= ARRAY_SIZE(gpio_buttons),
	.rep		= 1,
	.buttons_reserved	= buttons_joy,
	.nbuttons_reserved	= ARRAY_SIZE(buttons_joy),
};

static struct platform_device omap3pandora_keys_gpio = {
	.name	= "gpio-keys",
	.id	= -1,
	.dev	= {
		.platform_data	= &gpio_key_info,
	},
};

static int pandora_keys_gpio_mode;

static int pandora_input_proc_read(char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	*eof = 1;
	return sprintf(page, "%d\n", pandora_keys_gpio_mode);
}

static int pandora_input_proc_write(struct file *file,
		const char __user *buffer, unsigned long count, void *data)
{
	const unsigned short *buttons;
	int i, val, button_count;
	char s[32];

	if (!count)
		return 0;
	if (count > 31)
		return -EINVAL;
	if (copy_from_user(s, buffer, count))
		return -EFAULT;
	s[count] = 0;
	if (sscanf(s, "%i", &val) != 1)
		return -EINVAL;

	if (val == 1) {
		buttons = buttons_kbd;
		button_count = ARRAY_SIZE(buttons_kbd);
	} else if (val == 2) {
		buttons = buttons_joy;
		button_count = ARRAY_SIZE(buttons_joy);
	} else
		return -EINVAL;

	for (i = 0; i < button_count; i++)
		gpio_buttons[i].code = buttons[i];

	pandora_keys_gpio_mode = val;
	return count;
}

void __init omap3pandora_input_init(void)
{
	struct proc_dir_entry *pret;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(gpio_buttons); i++)
		omap_set_gpio_debounce(gpio_buttons[i].gpio, 1);

	/* set debounce time for banks 4 and 6 */
	omap_set_gpio_debounce_time(32 * 3, GPIO_DEBOUNCE_TIME);
	omap_set_gpio_debounce_time(32 * 5, GPIO_DEBOUNCE_TIME);

	ret = platform_device_register(&omap3pandora_keys_gpio);
	if (ret != 0) {
		pr_err("Failed to register gpio-keys\n");
		return;
	}

	pret = create_proc_entry(GPIO_KEYS_PROC, S_IWUGO | S_IRUGO, NULL);
	if (pret == NULL) {
		proc_mkdir("pandora", NULL);
		pret = create_proc_entry(GPIO_KEYS_PROC, S_IWUGO | S_IRUGO, NULL);
		if (pret == NULL)
			pr_err("pandora_input: can't create proc file");
	}

	if (pret != NULL) {
		pret->read_proc = pandora_input_proc_read;
		pret->write_proc = pandora_input_proc_write;
	}

	/* kbd mode by default */
	pandora_keys_gpio_mode = 1;
}

