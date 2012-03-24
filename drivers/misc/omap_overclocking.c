/*
 * OMAP CPU overclocking hacks
 *
 * Licensed under the GPL-2 or later.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/opp.h>
#include <linux/clk.h>
#include <linux/uaccess.h>

#include <plat/omap_device.h>

#define PROC_DIR	"pandora"
#define PROC_CPUMHZ	"pandora/cpu_mhz_max"
#define PROC_CPUOPP	"pandora/cpu_opp_max"
#define PROC_SYSMHZ	"pandora/sys_mhz_max"

/* FIXME: could use opp3xxx_data.c, but that's initdata.. */
static const unsigned long nominal_freqs_35xx[] = {
	125000000, 250000000, 500000000, 550000000, 600000000,
};

static const unsigned long nominal_freqs_36xx[] = {
	300000000, 600000000, 800000000, 1000000000,
};

static const unsigned long *nominal_freqs;

static int opp_max_avail, opp_max;

static int set_opp_max(int new_opp_max)
{
	struct device *mpu_dev;
	int i, ret;

	if (new_opp_max == opp_max)
		return 0;

	mpu_dev = omap_device_get_by_hwmod_name("mpu");
	if (IS_ERR(mpu_dev)) {
		pr_err("%s: mpu device not available (%ld)\n",
			__func__, PTR_ERR(mpu_dev));
		return -ENODEV;
	}

	for (i = 1; i < new_opp_max; i++) {
		ret = opp_enable_i(mpu_dev, i);
		if (ret != 0)
			dev_err(mpu_dev, "%s: opp_enable returned %d\n",
				__func__, ret);
	}

	for (i = new_opp_max; i < opp_max_avail; i++) {
		ret = opp_disable_i(mpu_dev, i);
		if (ret != 0)
			dev_err(mpu_dev, "%s: opp_disable returned %d\n",
				__func__, ret);
	}

	opp_max = new_opp_max;
	dev_info(mpu_dev, "max OPP set to %d\n", opp_max);

	return 0;
}

static int set_cpu_mhz_max(unsigned long new_mhz_max)
{
	unsigned long cur_mhz_max = 0;
	struct device *mpu_dev;
	int index, ret;

	new_mhz_max *= 1000000;

	if (opp_max < 1 || opp_max > opp_max_avail) {
		pr_err("%s: corrupt opp_max: %d\n", __func__, opp_max);
		return -EINVAL;
	}
	index = opp_max - 1;

	/* going below nominal makes no sense, it will save us nothing,
	 * user should reduce max OPP instead */
	if (new_mhz_max < nominal_freqs[index])
		new_mhz_max = nominal_freqs[index];

	mpu_dev = omap_device_get_by_hwmod_name("mpu");
	if (IS_ERR(mpu_dev)) {
		pr_err("%s: mpu device not available (%ld)\n",
			__func__, PTR_ERR(mpu_dev));
		return -ENODEV;
	}

	opp_hack_get_freq(mpu_dev, index, &cur_mhz_max);
	if (cur_mhz_max == new_mhz_max)
		return 0;

	ret = opp_hack_set_freq(mpu_dev, index, new_mhz_max);
	if (ret != 0) {
		dev_err(mpu_dev, "%s: opp_hack_set_freq returned %d\n",
			__func__, ret);
		return ret;
	}

	return 0;
}

static int get_cpu_mhz_max(void)
{
	unsigned long cur_mhz_max = 0;
	struct device *mpu_dev;

	if (opp_max < 1 || opp_max > opp_max_avail) {
		pr_err("%s: corrupt opp_max: %d\n", __func__, opp_max);
		return -EINVAL;
	}

	mpu_dev = omap_device_get_by_hwmod_name("mpu");
	if (IS_ERR(mpu_dev)) {
		pr_err("%s: mpu device not available (%ld)\n",
			__func__, PTR_ERR(mpu_dev));
		return -ENODEV;
	}

	opp_hack_get_freq(mpu_dev, opp_max - 1, &cur_mhz_max);

	return cur_mhz_max / 1000000;
}

static int init_opp_hacks(void)
{
	struct device *mpu_dev;

	mpu_dev = omap_device_get_by_hwmod_name("mpu");
	if (IS_ERR(mpu_dev)) {
		pr_err("%s: mpu device not available (%ld)\n",
			__func__, PTR_ERR(mpu_dev));
		return -ENODEV;
	}

	if (cpu_is_omap3630()) {
		nominal_freqs = nominal_freqs_36xx;
		opp_max_avail = sizeof(nominal_freqs_36xx) / sizeof(nominal_freqs_36xx[0]);
		opp_max = 2;
	} else if (cpu_is_omap34xx()) {
		nominal_freqs = nominal_freqs_35xx;
		opp_max_avail = sizeof(nominal_freqs_35xx) / sizeof(nominal_freqs_35xx[0]);
		opp_max = opp_max_avail;
	} else {
		dev_err(mpu_dev, "%s: unsupported CPU\n", __func__);
		return -ENODEV;
	}

	return 0;
}

static int set_sys_mhz_max(unsigned long rate)
{
	struct clk *dpll3_m2_ck;
	int ret;

	rate *= 1000000;

	dpll3_m2_ck = clk_get(NULL, "dpll3_m2_ck");
	if (IS_ERR(dpll3_m2_ck)) {
		pr_err("%s: dpll3_m2_clk not available: %ld\n",
			__func__, PTR_ERR(dpll3_m2_ck));
		return -ENODEV;
	}

	pr_info("Reprogramming CORE clock to %luHz\n", rate);
	ret = clk_set_rate(dpll3_m2_ck, rate);
	if (ret)
		pr_err("dpll3_m2_clk rate change failed: %d\n", ret);

	clk_put(dpll3_m2_ck);

	return ret;
}

static int get_sys_mhz_max(void)
{
	struct clk *dpll3_m2_ck;
	int ret;

	dpll3_m2_ck = clk_get(NULL, "dpll3_m2_ck");
	if (IS_ERR(dpll3_m2_ck)) {
		pr_err("%s: dpll3_m2_clk not available: %ld\n",
			__func__, PTR_ERR(dpll3_m2_ck));
		return -ENODEV;
	}

	ret = clk_get_rate(dpll3_m2_ck);
	clk_put(dpll3_m2_ck);

	return ret / 1000000;
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
		pr_err("error %i parsing %s\n", ret, buff);
		return ret;
	}

	return count;
}

static int cpu_clk_read(char *page, char **start, off_t off, int count,
		int *eof, void *data)
{
	return proc_read_val(page, start, off, count, eof, get_cpu_mhz_max());
}

static int cpu_clk_write(struct file *file, const char __user *buffer,
		unsigned long count, void *data)
{
	unsigned long val;
	int ret, retval;

	retval = proc_write_val(file, buffer, count, &val);
	if (retval < 0)
		return retval;

	ret = set_cpu_mhz_max(val);
	if (ret < 0)
		return ret;

	return retval;
}

static int cpu_maxopp_read(char *page, char **start, off_t off, int count,
		int *eof, void *data)
{
	return proc_read_val(page, start, off, count, eof, opp_max);
}

static int cpu_maxopp_write(struct file *file, const char __user *buffer,
		unsigned long count, void *data)
{
	unsigned long val;
	int ret, retval;

	retval = proc_write_val(file, buffer, count, &val);
	if (retval < 0)
		return retval;

	if (val < 1 || val > opp_max_avail)
		return -EINVAL;

	ret = set_opp_max(val);
	if (ret != 0)
		return ret;

	return retval;
}

static int sys_clk_read(char *page, char **start, off_t off, int count,
		int *eof, void *data)
{
	return proc_read_val(page, start, off, count, eof, get_sys_mhz_max());
}

static int sys_clk_write(struct file *file, const char __user *buffer,
		unsigned long count, void *data)
{
	unsigned long val;
	int ret, retval;

	retval = proc_write_val(file, buffer, count, &val);
	if (retval < 0)
		return retval;

	ret = set_sys_mhz_max(val);
	if (ret < 0)
		return ret;

	return retval;
}

static void proc_create_rw(const char *name, void *pdata,
			   read_proc_t *read_proc, write_proc_t *write_proc)
{
	struct proc_dir_entry *pret;

	pret = create_proc_entry(name, S_IWUSR | S_IRUGO, NULL);
	if (pret == NULL) {
		proc_mkdir(PROC_DIR, NULL);
		pret = create_proc_entry(name, S_IWUSR | S_IRUGO, NULL);
		if (pret == NULL) {
			pr_err("%s: failed to create proc file %s\n",
				__func__, name);
			return;
		}
	}

	pret->data = pdata;
	pret->read_proc = read_proc;
	pret->write_proc = write_proc;
}

static int pndctrl_init(void)
{
	int ret;

	ret = init_opp_hacks();
	if (ret != 0) {
		pr_err("init_opp_hacks failed: %d\n", ret);
		return -EFAULT;
	}

	proc_create_rw(PROC_CPUMHZ, NULL, cpu_clk_read, cpu_clk_write);
	proc_create_rw(PROC_CPUOPP, NULL, cpu_maxopp_read, cpu_maxopp_write);
	proc_create_rw(PROC_SYSMHZ, NULL, sys_clk_read, sys_clk_write);

	pr_info("OMAP overclocker loaded.\n");
	return 0;
}


static void pndctrl_cleanup(void)
{
	remove_proc_entry(PROC_CPUOPP, NULL);
	remove_proc_entry(PROC_CPUMHZ, NULL);
	remove_proc_entry(PROC_SYSMHZ, NULL);
}

module_init(pndctrl_init);
module_exit(pndctrl_cleanup);

MODULE_AUTHOR("Gražvydas Ignotas");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("OMAP overclocking support");
