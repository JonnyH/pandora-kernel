/*
 * board-omap3pandora.h
 *
 * Hardware definitions for OMAP3 Pandora.
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
 */

#ifndef __ASM_ARCH_OMAP3_PANDORA_H
#define __ASM_ARCH_OMAP3_PANDORA_H

#include <linux/device.h>
#include <linux/i2c/twl4030.h>

extern struct twl4030_keypad_data omap3pandora_kp_data;

void __init omap3pandora_input_init(void);

#define	OMAP3_PANDORA_TS_GPIO		94

#endif /* __ASM_ARCH_OMAP3_PANDORA_H */
