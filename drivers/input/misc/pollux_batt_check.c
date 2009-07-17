/*
 * pollux_pollux_batt.c
 *
 * Copyright    (C) 2007 MagicEyes Digital Co., Ltd.
 * 
 * godori(Ko Hyun Chul), omega5                 							- project manager
 * gtgkjh(Kim Jung Han), choi5946(Choi Hyun Jin), nautilus_79(Lee Jang Ho) 	- main programmer
 * amphell(Bang Chang Hyeok)                        						- Hardware Engineer
 *
 * 2003-2007 AESOP-Embedded project
 *	           http://www.aesop-embedded.org/mp2530/index.html
 *
 * This code is released under both the GPL version 2 and BSD licenses.
 * Either license may be used.  The respective licenses are found at
 * below.
 *
 * - GPL -
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
 *
 * - BSD -
 *
 *    In addition:
 * 
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *   3. The name of the author may not be used to endorse or promote
 *      products derived from this software without specific prior written
 *      permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 *   IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 *   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *   ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 *   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *   (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *   HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 *   STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 *   IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *   POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <linux/version.h>
#include <linux/module.h>
#if defined(MODVERSIONS)
#include <linux/modversions.h>
#endif
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/hardware.h>
#include <linux/types.h>
#include <linux/sched.h>

#include <linux/mmc/host.h>
#include <asm/arch/regs-gpio.h>
#include <asm/arch/pollux.h>

#include "../touchscreen/pollux_ts/pollux-adc.h"

#define DRV_NAME "pollux_batt"
#define POLLUX_BATT_MINOR       159

#define CHECK_BATTRY_TIME       3000 //1000
#define CHECK_BATTRY_TIME_LOW   700

#define GPIO_POWER_OFF			POLLUX_GPA18
#define GPIO_LCD_AVDD			POLLUX_GPB14
#define USB_VBUS_DECT	        POLLUX_GPC15
#define BD_VER_LSB              POLLUX_GPC18
#define BD_VER_MSB              POLLUX_GPC17


struct batt_timer {
	struct timer_list	batt_chkTimer;
	int power_switch_off; 
	int timer_done;
	struct work_struct	work;
    int startTimer;
    int offCnt;
    int status;
    
};

extern asmlinkage long sys_umount(char* name, int flags);

struct batt_timer *bTimer; 

#define     LIMIT3_9V_NEW       0x390
#define     LIMIT3_7V_NEW       0x365        

#ifdef CONFIG_ARCH_ADD_GPH_F300
#define     LIMIT3_5V_NEW       0x345      
#else
#define     LIMIT3_5V_NEW       0x342
#endif


#define     LIMIT3_9V_OLD       0x1D5
#define     LIMIT3_7V_OLD       0x1C0        
#define     LIMIT3_5V_OLD       0x1A9      


#define     BATTRY_OFF_TIME_OLD_BOARD     0x19b  
#ifdef CONFIG_ARCH_ADD_GPH_F300
#define     BATTRY_OFF_TIME_NEW_BOARD     0x335  
#else
#define     BATTRY_OFF_TIME_NEW_BOARD     0x33A  
#endif
#define     BATTRY_READ_CNT     5

enum {
    BATT_LEVEL_HIGH,
    BATT_LEVEL_MID,
    BATT_LEVEL_LOW,
    BATT_LEVEL_EMPTY,
}BATT_LEVEL_STATUS;    

unsigned short old_adc = 0;
static int bd_rev;
static unsigned short batt_off_time;
static unsigned short limit_39V;
static unsigned short limit_37V;
static unsigned short limit_35V;

static int get_battADC(unsigned short* adc)
{
    unsigned char Timeout = 0xff;
    
    MES_ADC_SetInputChannel(3);
	MES_ADC_Start();
	
	while(!MES_ADC_IsBusy() && Timeout--);	// for check between start and busy time
	while(MES_ADC_IsBusy());
	*adc = (U16)MES_ADC_GetConvertedData();
    return 0;
}


static void batt_level_chk_timer(unsigned long data)
{
	struct batt_timer *btimer = (struct batt_timer *)data;
	unsigned short read_adc;
	
	get_battADC(&read_adc);
	//printk("TIME: adcVal:0x%x \n",read_adc);

    if(bTimer->offCnt >= 2)
	    schedule_work(&btimer->work);
	else{
        if(read_adc <= batt_off_time) {
            bTimer->offCnt += 1;
	        mod_timer(&bTimer->batt_chkTimer, jiffies + CHECK_BATTRY_TIME);
	    }else{
	        mod_timer(&bTimer->batt_chkTimer, jiffies + CHECK_BATTRY_TIME);
        }
    }
}

static void batt_chk_time_work_handler(struct work_struct *work)
{
	struct batt_timer *btimer = container_of(work, struct batt_timer, work);
    unsigned short read_adc;
    char *argv[3], **envp, *buf, *scratch;
	int i = 0,value;
    
    get_battADC(&read_adc);
    if(read_adc <= batt_off_time)
	{
	    printk("battry low level => power off \n");

	    //Pwr_Off_Enable();
        if (!(envp = (char **) kmalloc(20 * sizeof(char *), GFP_KERNEL))) {
		    printk(KERN_ERR "input.c: not enough memory allocating hotplug environment\n");
		    return;
	    }
    
        if (!(buf = kmalloc(1024, GFP_KERNEL))) {
		    kfree (envp);
		    printk(KERN_ERR "pwrbtn: not enough memory allocating hotplug environment\n");
		    return;
	    }
    
        argv[0] = "/etc/hotplug.d/default/default.hotplug";
	    argv[1] = "power_off";
	    argv[2] = NULL;
    
        envp[i++] = "HOME=/";
	    envp[i++] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";
    
	    scratch = buf;

	    envp[i++] = scratch;
#if 0	   
	    scratch += sprintf(scratch, "ACTION=%s", "remove") + 1;
#else
        scratch += sprintf(scratch, "ACTION=%s", "change") + 1;
#endif    
        envp[i++] = NULL;
   
        value = call_usermodehelper(argv [0], argv, envp, 0);
    
        kfree(buf);
	    kfree(envp);
	
	}	    


	mod_timer(&bTimer->batt_chkTimer, jiffies + CHECK_BATTRY_TIME_LOW);
}

static int pollux_batt_open(struct inode *inode, struct file *filp)
{
	
	if(bTimer->startTimer == 0) {
	    mod_timer(&bTimer->batt_chkTimer, jiffies + CHECK_BATTRY_TIME);
	    pollux_sdi_probe1();
        bTimer->startTimer = 1;
    } 

    
    return 0;
}


static int pollux_batt_release(struct inode *inode, struct file *filp)
{
	
	return 0;
}

static int pollux_batt_read(struct file *filp, char __user *buffer, size_t count, loff_t *ppos)
{
    unsigned short readADC;
    unsigned short battStatus;
    unsigned short r[BATTRY_READ_CNT];
    unsigned long sum=0;
    unsigned short min, max, temp; 
    int usb_in;
    int i;
    
    for(i = 0; i < BATTRY_READ_CNT ; i++) {
        get_battADC(&readADC);
        r[i]= readADC; 
    }
    
    if( r[0] > r[1] ){
         min = r[1]; 
         max = r[0];       
    }else if( r[0] < r[1] ){
        min = r[0];
        max = r[1];       
    }else  min=max=r[0];   
    
    for(i = 2; i < BATTRY_READ_CNT ; i++)
    {
        if ( min > r[i] ){
            temp = min;
            min = r[i];
            r[i] = temp;
        }      
        
        if ( max < r[i] ){
            temp = max;
            max = r[i];
            r[i] = temp;
        }
        
        sum+=r[i];
    }
    
    readADC = (unsigned short) (sum / ( BATTRY_READ_CNT - 2 ));
    
       
#ifdef CONFIG_ARCH_ADD_GPH_F300
    if( (bd_rev == 0) ||  (bd_rev == 1) ) goto NOT_SUPPORT_USB_REMOVE;
#else
    if( (bd_rev == 0) ) goto NOT_SUPPORT_USB_REMOVE;
#endif

    if(old_adc)
    {
        usb_in = pollux_gpio_getpin(USB_VBUS_DECT);
        if( !usb_in ){       
            //No charge mode 
            if( old_adc <= readADC)  readADC = old_adc;
            else old_adc = readADC;
        }
    } else {
         old_adc = readADC;
    }
    
NOT_SUPPORT_USB_REMOVE:
    
//    printk("readadc:0x%x \n", readADC);
    if(readADC  > limit_39V ) 
		battStatus = 1;
	else if(readADC  > limit_37V ) 
		battStatus = 2;
	else if(readADC  > limit_35V ) 
		battStatus = 3;
	else 
		battStatus = 4;	/* 0x1A4 ~ 0x19C */
    
    if( copy_to_user (buffer, &battStatus, sizeof(unsigned short)) )
        return -EFAULT;
        
    return sizeof(unsigned short);
}

#if 0
int pollux_batt_ioctl(struct inode *inode, struct file *filp, unsigned int cmd,unsigned long arg)
{
	int ret;

	switch(cmd)
    {
    
    }
}
#endif


static struct file_operations pollux_batt_fops = {
	.owner		= THIS_MODULE,
    .open       = pollux_batt_open,
    .read		= pollux_batt_read,
    //.ioctl      = pollux_batt_ioctl,    
    .release    = pollux_batt_release,
};


static struct miscdevice pollux_batt_misc_device = {
	POLLUX_BATT_MINOR,
	DRV_NAME,
	&pollux_batt_fops,
};

int __init pollux_batt_init(void)
{
    int ret;
    
    if( pollux_gpio_getpin(BD_VER_LSB) && (!pollux_gpio_getpin(BD_VER_MSB)) )
        bd_rev = 1;
    else if( (!pollux_gpio_getpin(BD_VER_LSB)) && pollux_gpio_getpin(BD_VER_MSB) )
        bd_rev = 2;
    else if( pollux_gpio_getpin(BD_VER_LSB) && pollux_gpio_getpin(BD_VER_MSB) )
        bd_rev = 3;
    else
        bd_rev = 0;
        
	
#ifdef CONFIG_ARCH_ADD_GPH_F300	
    if( (bd_rev == 0) ||  (bd_rev == 1) || (bd_rev == 2) ){
        batt_off_time = BATTRY_OFF_TIME_OLD_BOARD;
        limit_39V = LIMIT3_9V_OLD;
        limit_37V = LIMIT3_7V_OLD;
        limit_35V = LIMIT3_5V_OLD;
    }
    else{ 
        batt_off_time  = BATTRY_OFF_TIME_NEW_BOARD;
        limit_39V = LIMIT3_9V_NEW;
        limit_37V = LIMIT3_7V_NEW;
        limit_35V = LIMIT3_5V_NEW;
    }
#else /* wiz */
    if( (bd_rev == 0) ){
        batt_off_time = BATTRY_OFF_TIME_OLD_BOARD;
        limit_39V = LIMIT3_9V_OLD;
        limit_37V = LIMIT3_7V_OLD;
        limit_35V = LIMIT3_5V_OLD;
    }
    else{
        batt_off_time = BATTRY_OFF_TIME_NEW_BOARD;
        limit_39V = LIMIT3_9V_NEW;
        limit_37V = LIMIT3_7V_NEW;
        limit_35V = LIMIT3_5V_NEW;
    } 
    
#endif
	
	
	bTimer = kzalloc(sizeof(struct batt_timer), GFP_KERNEL);
	if (unlikely(!bTimer))
		return -ENOMEM;
		
	INIT_WORK(&bTimer->work, batt_chk_time_work_handler);
	init_timer(&bTimer->batt_chkTimer);
	
	bTimer->batt_chkTimer.function = batt_level_chk_timer;
	bTimer->batt_chkTimer.data = (unsigned long)bTimer;
	bTimer->startTimer = 0;
	bTimer->offCnt = 0;
	
	//mod_timer(&bTimer->batt_chkTimer, jiffies + CHECK_BATTRY_TIME);
	
	if (misc_register (&pollux_batt_misc_device)) {
		printk (KERN_WARNING "pwrbton: Couldn't register device 10, "
				"%d.\n", POLLUX_BATT_MINOR);
		kfree(bTimer);
		return -EBUSY;
	}



#ifdef 	CONFIG_POLLUX_KERNEL_BOOT_MESSAGE_ENABLE    
    printk("pollux_batt control device driver install\n");
#endif    
    return 0;	

}

void __exit
pollux_batt_exit(void)
{
	flush_scheduled_work();
	kfree(bTimer);
	misc_deregister (&pollux_batt_misc_device);
	printk("pollux power off button check device driver uinstall\n");
    
}

module_init(pollux_batt_init);
module_exit(pollux_batt_exit);

MODULE_AUTHOR("godori working");
MODULE_DESCRIPTION("pollux battry driver");
MODULE_LICENSE("GPL"); 
