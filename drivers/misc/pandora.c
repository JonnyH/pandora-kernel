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
#include <linux/i2c/twl4030.h>

#ifndef CONFIG_PROC_FS
#error need CONFIG_PROC_FS
#endif

#define PND_PROC_CPUMHZ		"pandora/cpu_mhz_max"
#define PND_PROC_CPUOPP		"pandora/cpu_opp_max"

static int max_allowed_opp = 3; /* 3 on ED's request */
static int opp;

static void set_opp(int nopp)
{
	static const unsigned char opp2volt[5] = { 30, 38, 48, 54, 60 };
	unsigned char v, ov;

	if (nopp>5) nopp=5;
	if (nopp<1) nopp=1;
	v = opp2volt[nopp-1];
	twl4030_i2c_write_u8(TWL4030_MODULE_PM_RECEIVER, v, 0x5E);

	ov = opp2volt[opp-1];
	/*
	 * Rationale: Slowest slew rate of the TPS65950 SMPS is 4 mV/us.
	 * Each unit is 12.5mV, resulting in 3.125us/unit.
	 * Rounded up for a safe 4us/unit.
	*/
	udelay(abs(v - ov) * 4);

	opp = nopp;
	printk(KERN_INFO "set_opp: OPP %d set\n", opp);
}

static int mhz2opp(int mhz)
{
	int new_opp=1;
	if (mhz<14) new_opp=3; /* Guard against -1 */
	if (mhz>125) new_opp=2;
	if (mhz>250) new_opp=3;
	if (mhz>600) new_opp=4; /* Assumption: current pandoras OK with 600Mhz@OPP3 */
	if (mhz>720) new_opp=5;

	if (max_allowed_opp > 0 && new_opp > max_allowed_opp)
		new_opp = max_allowed_opp;

	return new_opp;
}

static void preop_oppset(int mhz)
{
	int new_opp = mhz2opp(mhz);
	if (new_opp > opp)
		set_opp(new_opp);
}

static void postop_oppset(int mhz)
{
	int new_opp = mhz2opp(mhz);
	if (new_opp < opp)
		set_opp(new_opp);
}

/*
 * note:
 * SYS_CLK is 26 MHz (see PRM_CLKSEL)
 * ARM_FCLK = (SYS_CLK * M * 2) / ([N+1] * M2) / 2
 * CM_CLKSEL1_PLL_MPU = N | (M << 8) | (M2 << 19)
 */
static int get_fclk(void)
{
	unsigned __iomem *base;
	unsigned n, m, m2;
	unsigned ret;

	base = ioremap(0x48004000, 0x2000);
	if (base == NULL) {
		printk(KERN_ERR "get_fclk: can't ioremap\n");
		return -1;
	}

	ret = base[0x940 >> 2];
	iounmap(base);

	n = ret & 0xff;
	m = (ret >> 8) & 0x7ff;
	m2 = (ret >> 19) & 7;

	if (m2 != 1 && m2 != 2 && m2 != 4) {
		printk(KERN_ERR "get_fclk: invalid divider %d\n", m2);
		return -1;
	}

	return 26 * m / ((n + 1) * m2);
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

	preop_oppset(val);
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
	postop_oppset(val);
}

static int proc_read_val(char *page, char **start, off_t off, int count,
		int *eof, int val)
{
	char *p = page;
	int len;

	p += sprintf(p, "%d\n", val);

	len = (p - page) - off;
	if (len < 0)
		len = 0;

	*eof = (len <= count) ? 1 : 0;
	*start = page + off;

	return len;
}

static int proc_write_val(struct file *file, const char __user *buffer,
		unsigned long count, unsigned long *val)
{
	char buff[32];
	int ret;

	count = strncpy_from_user(buff, buffer,
			count < sizeof(buff) ? count : sizeof(buff) - 1);
	buff[count] = 0;

	ret = strict_strtoul(buff, 0, val);
	if (ret < 0) {
		printk(KERN_ERR "error %i parsing %s\n", ret, buff);
		return ret;
	}

	return count;
}

static int cpu_clk_read(char *page, char **start, off_t off, int count,
		int *eof, void *data)
{
	return proc_read_val(page, start, off, count, eof, get_fclk());
}

static int cpu_clk_write(struct file *file, const char __user *buffer,
		unsigned long count, void *data)
{
	unsigned long val;
	int ret;

	ret = proc_write_val(file, buffer, count, &val);
	if (ret < 0)
		return ret;

	set_fclk(val);
	return ret;
}

static int cpu_maxopp_read(char *page, char **start, off_t off, int count,
		int *eof, void *data)
{
	return proc_read_val(page, start, off, count, eof, max_allowed_opp);
}

static int cpu_maxopp_write(struct file *file, const char __user *buffer,
		unsigned long count, void *data)
{
	unsigned long val;
	int ret;

	ret = proc_write_val(file, buffer, count, &val);
	if (ret < 0)
		return ret;

	if (val < 1 || val > 6)
		return -EINVAL;

	max_allowed_opp = val;
	return ret;
}

static void proc_create_rw(const char *name, void *pdata,
			   read_proc_t *read_proc, write_proc_t *write_proc)
{
	struct proc_dir_entry *pret;
	
	pret = create_proc_entry(name, S_IWUGO | S_IRUGO, NULL);
	if (pret == NULL) {
		proc_mkdir("pandora", NULL);
		pret = create_proc_entry(name, S_IWUGO | S_IRUGO, NULL);
		if (pret == NULL) {
			printk(KERN_ERR "failed to create proc file %s\n", name);
			return;
		}
	}

	pret->data = pdata;
	pret->read_proc = read_proc;
	pret->write_proc = write_proc;
}

/* ************************************************************************* */

static int pndctrl_init(void)
{
	opp = mhz2opp(get_fclk());

	proc_create_rw(PND_PROC_CPUMHZ, NULL, cpu_clk_read, cpu_clk_write);
	proc_create_rw(PND_PROC_CPUOPP, NULL, cpu_maxopp_read, cpu_maxopp_write);

	printk(KERN_INFO "pndctrl loaded.\n");
	return 0;
}


static void pndctrl_cleanup(void)
{
	remove_proc_entry(PND_PROC_CPUOPP, NULL);
	remove_proc_entry(PND_PROC_CPUMHZ, NULL);
	printk(KERN_INFO "pndctrl unloaded.\n");
}


module_init(pndctrl_init);
module_exit(pndctrl_cleanup);

MODULE_AUTHOR("Grazvydas Ignotas");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Pandora Additional hw control");
