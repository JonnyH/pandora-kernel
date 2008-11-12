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
#include <asm/io.h>

/* HACK */
#include <../drivers/video/omap/dispc.h>

#ifndef CONFIG_PROC_FS
#error need CONFIG_PROC_FS
#endif

#define PND_PROC_DIR		"pandora"
#define PND_PROC_CPUMHZ		"cpu_mhz_max"
#define PND_PROC_VSYNC		"wait_vsync"

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

static DECLARE_WAIT_QUEUE_HEAD(pnd_vsync_waitq);
static u32 pnd_vsync_counter;
static u32 pnd_vsync_last;
static u32 pnd_vsync_active;

static void pnd_vsync_callback(void *data)
{
	pnd_vsync_counter++;
	if (pnd_vsync_active)
		wake_up_interruptible(&pnd_vsync_waitq);
}

static int pnd_hook_vsync(void)
{
	unsigned __iomem *base;
	u32 val;
	int ret;

	ret = omap_dispc_request_irq(2, pnd_vsync_callback, NULL);
	if (ret) {
		printk(KERN_ERR "failed to get irq from dispc driver\n");
		return -1;
	}

	base = ioremap(0x48050400, 0x400);
	if (!base) {
		omap_dispc_free_irq(2, pnd_vsync_callback, NULL);
		printk(KERN_ERR "can't ioremap DISPC\n");
		return -1;
	}

	val = __raw_readl(base + 0x1c);
	//printk("val: %08x\n", val);
	val |= 2;
	__raw_writel(val, base + 0x1c);

	iounmap(base);
	return 0;
}

static void pnd_unhook_vsync(void)
{
	unsigned __iomem *base;

	base = ioremap(0x48050400, 0x400);
	if (!base) {
		printk(KERN_ERR "can't ioremap DISPC\n");
	}
	else {
		u32 val = __raw_readl(base + 0x1c);
		val &= ~2;
		__raw_writel(val, base + 0x1c);
		iounmap(base);
	}

	omap_dispc_free_irq(2, pnd_vsync_callback, NULL);
}

static int pnd_vsync_read(char *page, char **start, off_t off, int count,
		int *eof, void *data)
{
	char *p = page;
	int len, val = -1, ret, vcount;

	vcount = pnd_vsync_counter;
	pnd_vsync_active = 1;
	ret = wait_event_interruptible_timeout(pnd_vsync_waitq,
			(vcount != pnd_vsync_counter), msecs_to_jiffies(250));
	pnd_vsync_active = 0;
	if (ret > 0)
		val = pnd_vsync_counter - pnd_vsync_last;

	p += sprintf(p, "%d\n", val);
	pnd_vsync_last = pnd_vsync_counter;

	len = (p - page) - off;
	if (len < 0)
		len = 0;

	*eof = (len <= count) ? 1 : 0;
	*start = page + off;

	return len;
}

/* ************************************************************************* */

static struct proc_dir_entry *proc_dir;

static int pndctrl_init(void)
{
	struct proc_dir_entry *pret;
	int ret = -ENOMEM;

	proc_dir = proc_mkdir(PND_PROC_DIR, NULL);
	if (proc_dir == NULL) {
		printk(KERN_ERR "can't create proc dir.\n");
		return -ENOMEM;
	}

	pret = create_proc_entry(PND_PROC_CPUMHZ,
			S_IWUSR | S_IRUGO, proc_dir);
	if (pret == NULL) {
		printk(KERN_ERR "can't create proc\n");
		goto fail0;
	}

	pret->read_proc = cpu_clk_read;
	pret->write_proc = cpu_clk_write;

	pret = create_proc_entry(PND_PROC_VSYNC,
			S_IRUGO, proc_dir);
	if (pret == NULL) {
		printk(KERN_ERR "can't create proc\n");
		goto fail1;
	}

	pret->read_proc = pnd_vsync_read;

	ret = pnd_hook_vsync();
	if (ret) {
		printk(KERN_ERR "couldn't hook vsync\n");
		goto fail2;
	}

	printk(KERN_INFO "pndctrl loaded.\n");

	return 0;

fail2:
	remove_proc_entry(PND_PROC_VSYNC, proc_dir);
fail1:
	remove_proc_entry(PND_PROC_CPUMHZ, proc_dir);
fail0:
	remove_proc_entry(PND_PROC_DIR, NULL);
	return ret;
}


static void pndctrl_cleanup(void)
{
	pnd_unhook_vsync();
	remove_proc_entry(PND_PROC_VSYNC, proc_dir);
	remove_proc_entry(PND_PROC_CPUMHZ, proc_dir);
	remove_proc_entry(PND_PROC_DIR, NULL);
	printk("pndctrl unloaded.\n");
}


module_init(pndctrl_init);
module_exit(pndctrl_cleanup);

MODULE_AUTHOR("Grazvydas Ignotas");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Pandora Additional hw control");
