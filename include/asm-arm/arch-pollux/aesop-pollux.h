/* 
 * linux/include/asm-arm/arch-mp2530/mes-navikit.h
 *
 * Copyright. 2003-2007 AESOP-Embedded project
 *	           http://www.aesop-embedded.org/mp2530/index.html
 * 
 * godori(Ko Hyun Chul), omega5 - project manager
 * nautilus_79(Lee Jang Ho)     - main programmer
 * amphell(Bang Chang Hyeok)    - Hardware Engineer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */ 
#ifndef __AESOP_POLLUX_H
#define __AESOP_POLLUX_H

#ifndef __ASM_ARCH_HARDWARE_H
#error "include <asm/hardware.h> instead"
#endif

/*
 * SD/MMC Card Detection pin define
 */
#ifndef CONFIG_ARCH_POLLUX_GPH_GBOARD
#define IRQ_SD0_DETECT IRQ_GPIO_C(18)
#else
#define IRQ_SD0_DETECT IRQ_GPIO_A(19)
#endif

#define IRQ_SD1_DETECT IRQ_GPIO_A(28)


#ifndef CONFIG_ARCH_POLLUX_GPH_GBOARD
#define GPIO_SD0_CD POLLUX_GPC18
#define GPIO_SD0_WP POLLUX_GPC19
#else
#define GPIO_SD0_CD POLLUX_GPA19
#define GPIO_SD0_WP POLLUX_GPA20
#endif

#define GPIO_SD1_CD POLLUX_GPA28
#define GPIO_SD1_WP POLLUX_GPA29

#define POLLUX_FB_VIO_BASE	0xf4000000 
#define POLLUX_FB_PIO_BASE 	(PHYS_OFFSET + 0x2A00000) // 42MB
#define POLLUX_FB_MAP_SIZE  (0x400000) // 4M

#define POLLUX_SOUND_VIO_BASE	(POLLUX_FB_VIO_BASE+POLLUX_FB_MAP_SIZE) /* Framebuffer + 4M offset = sound dma buffer*/
#define POLLUX_SOUND_PIO_BASE 	(POLLUX_FB_PIO_BASE+POLLUX_FB_MAP_SIZE) 
#define	POLLUX_SOUND_MAP_SIZE	0x200000

#define TOTAL_DMA_MAP_SIZE  ( POLLUX_FB_MAP_SIZE + POLLUX_SOUND_MAP_SIZE ) 

#define POLLUX_MPEGDEC_VIO_BASE		0xf5000000 
#define POLLUX_MPEGDEC_PIO_BASE 	(PHYS_OFFSET + 0x3000000)  //48MB
#define POLLUX_MPEGDEC_MAP_SIZE  	(0x1000000) // 16MB



#define POLLUX_MPEG2D3DBUFFER_SIZE			0x1e00000
#define POLLUX_MPEG2D3D_STRIDE				4096

#define POLLUX_MPEG2D3D_BANK_SHIFT			20	// Fixed
#define POLLUX_MPEG2D3DBUFFER_PIO_BASE		( POLLUX_FB_PIO_BASE - POLLUX_MPEG2D3DBUFFER_SIZE )

#define MDK_ETH_VIO_BASE	0xf3000000UL  /* dm9000a */
#define MDK_ETH_PIO_BASE 	POLLUX_PA_CS1 

#endif /* __AESOP_POLLUX_H */
