#ifndef _LB035Q02_H
#define _LB035Q02_H

#include <asm/arch/regs-gpio.h>


typedef struct _lcd_seting {
	unsigned int reg;
	unsigned int* pVal;
}lcd_seting;

#define MAX_REG					24
#define LCDOFF_REG_NUMBER		2
#define DEVICEID_LGPHILIPS		0x70			

unsigned int REG_NO[]= {0x01, 0x2, 0x03, 0x04, 0x05, 0x06, 0x0A, 0x0B, 0x0D, 0x0E, 0x0F, 0x16, 0x17, 0x1E,
									0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x3A, 0x3B };

unsigned int valueREG[] = {0x6300, 0x200, 0x117, 0x4c7, 0xffC0, 0xe806, 0x4008, 0x0, 0x30, 0x2800, 0x0, 
								0x9F80, 0xA0F, 0xBD,0x300, 0x107, 0x0, 0x0, 0x707, 0x4, 0x302, 0x202, 0xA0D, 0x806 };


unsigned int LCDOFF_REGNO[]= {0x05, 0x01 };
unsigned int lcdoff_value[]= { 0, 0, };



#define CS_LOW	( pollux_gpio_setpin(POLLUX_GPB27 ,0))
#define CS_HIGH	( pollux_gpio_setpin(POLLUX_GPB27 ,1))
#define SCL_LOW ( pollux_gpio_setpin(POLLUX_GPB28 ,0))
#define SCL_HIGH ( pollux_gpio_setpin(POLLUX_GPB28 ,1))
#define SDA_LOW ( pollux_gpio_setpin(POLLUX_GPB29 ,0))
#define SDA_HIGH ( pollux_gpio_setpin(POLLUX_GPB29 ,1))

void lcdSetWrite(unsigned char id, lcd_seting *p);

#endif
