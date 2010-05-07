/*
	pandora.c
	Exports some additional hardware control to userspace.
	Written by Gražvydas "notaz" Ignotas <notasas@gmail.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; version 2 of the License.
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>

#ifndef CONFIG_PROC_FS
#error need CONFIG_PROC_FS
#endif

#define PND_PROC_CPUMHZ		"pandora/cpu_mhz_max"

/*
 * note:
 * SYS_CLK is 26 MHz (see PRM_CLKSEL)
 * ARM_FCLK = (SYS_CLK * M * 2) / ([N+1] * M2) / 2
 * CM_CLKSEL1_PLL_MPU = N | (M << 8) | (M2 << 19)
 */
static int get_fclk(void)
{
	unsigned __iomem *base;
	unsigned ret;

	base = ioremap(0x48004000, 0x2000);
	if (base == NULL) {
		printk(KERN_ERR "get_fclk: can't ioremap\n");
		return -1;
	}

	ret = base[0x940>>2];
	iounmap(base);

	if ((ret & ~0x7ff00) != 0x10000c) {
		printk(KERN_ERR "get_fclk: unexpected CM_CLKSEL1_PLL_MPU: "
				"%08x\n", ret);
		return -1;
	}

	ret &= 0x7ff00;
	ret >>= 8;

	return (int)ret;
}

static void set_fclk(int val)
{
	struct clk *pllclk, *fclk;
	int ret;

	if (val & ~0x7ff) {
		printk(KERN_ERR "set_fclk: value %d out of range\n", val);
		return;
	}


	pllclk = clk_get(NULL, "dpll1_ck");
	if (IS_ERR(pllclk)) {
		printk(KERN_ERR "set_fclk: clk_get() failed: %li\n",
				PTR_ERR(pllclk));
		return;
	}

	ret = clk_set_rate(pllclk, val * 1000000);
	if (ret)
		printk(KERN_ERR "set_fclk: clk_set_rate(dpll1_ck) "
				"failed: %li\n", PTR_ERR(pllclk));
	msleep(100);

	printk(KERN_INFO "dpll1_ck rate: %li\n", clk_get_rate(pllclk));
	clk_put(pllclk);

	fclk = clk_get(NULL, "arm_fck");
	if (!IS_ERR(pllclk)) {
		printk(KERN_INFO "arm_fck  rate: %li\n", clk_get_rate(fclk));
		clk_put(fclk);
	}

	printk(KERN_INFO "PLL_MPU  rate: %i\n", get_fclk() * 1000000);
}

static int cpu_clk_read(char *page, char **start, off_t off, int count,
		int *eof, void *data)
{
	char *p = page;
	int len;

	p += sprintf(p, "%d\n", get_fclk());

	len = (p - page) - off;
	if (len < 0)
		len = 0;

	*eof = (len <= count) ? 1 : 0;
	*start = page + off;

	return len;
}

static int cpu_clk_write(struct file *file, const char __user *buffer,
		unsigned long count, void *data)
{
	char buff[32];
	unsigned long val;
	int ret;

	count = strncpy_from_user(buff, buffer,
			count < sizeof(buff) ? count : sizeof(buff) - 1);
	buff[count] = 0;

	ret = strict_strtoul(buff, 0, &val);
	if (ret < 0) {
		printk(KERN_ERR "error %i parsing %s\n", ret, buff);
		return ret;
	}

	set_fclk(val);

	return count;
}

/* ************************************************************************* */

static int pndctrl_init(void)
{
	struct proc_dir_entry *pret;
	int ret = -ENOMEM;

	pret = create_proc_entry(PND_PROC_CPUMHZ, S_IWUSR | S_IRUGO, NULL);
	if (pret == NULL) {
		proc_mkdir("pandora", NULL);
		pret = create_proc_entry(PND_PROC_CPUMHZ,
					S_IWUSR | S_IRUGO, NULL);
		if (pret == NULL) {
			printk(KERN_ERR "can't create proc entry\n");
			return ret;
		}
	}

	pret->read_proc = cpu_clk_read;
	pret->write_proc = cpu_clk_write;

	printk(KERN_INFO "pndctrl loaded.\n");

	return 0;
}


static void pndctrl_cleanup(void)
{
	remove_proc_entry(PND_PROC_CPUMHZ, NULL);
	printk(KERN_INFO "pndctrl unloaded.\n");
}


module_init(pndctrl_init);
module_exit(pndctrl_cleanup);

MODULE_AUTHOR("Grazvydas Ignotas");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Pandora Additional hw control");
