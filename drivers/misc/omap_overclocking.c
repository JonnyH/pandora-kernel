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
#include <linux/regulator/consumer.h>

#include <plat/omap_device.h>

#define PROC_DIR	"pandora"
#define PROC_CPUMHZ	"pandora/cpu_mhz_max"
#define PROC_DSPMHZ	"pandora/dsp_mhz_max"
#define PROC_CPUOPP	"pandora/cpu_opp_max"
#define PROC_SYSMHZ	"pandora/sys_mhz_max"

static struct device *mpu_dev;

static struct device *iva_dev;
static struct regulator *iva_reg;
static struct clk *iva_clk;
static DEFINE_MUTEX(iva_lock);
static struct delayed_work iva_work;
static int iva_mhz_max;
static int iva_opp_min;
static int iva_active;

/* XXX: could use opp3xxx_data.c, but that's initdata.. */
static const unsigned long nominal_f_mpu_35xx[] = {
	125000000, 250000000, 500000000, 550000000, 600000000,
};

static const unsigned long nominal_f_mpu_36xx[] = {
	300000000, 600000000, 800000000, 1000000000,
};

static const unsigned long nominal_f_iva_35xx[] = {
	90000000,  180000000, 360000000, 400000000, 430000000,
};

static const unsigned long nominal_f_iva_36xx[] = {
	260000000, 520000000, 660000000, 800000000,
};

static const unsigned long *nominal_freqs_mpu;
static const unsigned long *nominal_freqs_iva;

/* IVA voltages (MPU ones are managed by cpufreq) */
static unsigned long iva_voltages[5];

static int opp_max_avail, opp_max_now, opp_max_ceil;

static int set_mpu_opp_max(int new_opp_max)
{
	int i, ret;

	if (new_opp_max == opp_max_now)
		return 0;

	for (i = 1; i < new_opp_max; i++) {
		ret = opp_enable_i(mpu_dev, i);
		if (ret != 0)
			dev_err(mpu_dev, "%s: mpu opp_enable returned %d\n",
				__func__, ret);
	}

	for (i = new_opp_max; i < opp_max_avail; i++) {
		ret = opp_disable_i(mpu_dev, i);
		if (ret != 0)
			dev_err(mpu_dev, "%s: mpu opp_disable returned %d\n",
				__func__, ret);
	}

	dev_info(mpu_dev, "max MPU OPP set to %d\n", new_opp_max);
	opp_max_now = new_opp_max;

	return 0;
}

static int set_opp_max_ceil(int new_opp_max)
{
	opp_max_ceil = new_opp_max;
	return set_mpu_opp_max(new_opp_max);
}

static int set_mpu_mhz_max(unsigned long new_mhz_max)
{
	unsigned long cur_mhz_max = 0;
	int index, ret;

	new_mhz_max *= 1000000;

	if (opp_max_ceil < 1 || opp_max_ceil > opp_max_avail) {
		pr_err("%s: corrupt opp_max_ceil: %d\n",
			__func__, opp_max_ceil);
		return -EINVAL;
	}

	/* determine minimum OPP needed for given MPU clock limit,
	 * and limit that opp as maximum OPP.
	 * This is for cpufreq governors only. */
	index = opp_max_ceil - 1;
	while (index > 0 && new_mhz_max <= nominal_freqs_mpu[index - 1])
		index--;

	set_mpu_opp_max(index + 1);

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

static int get_mpu_mhz_max(void)
{
	unsigned long cur_mhz_max = 0;

	if (opp_max_now < 1 || opp_max_now > opp_max_avail) {
		pr_err("%s: corrupt opp_max: %d\n", __func__, opp_max_now);
		return -EINVAL;
	}

	opp_hack_get_freq(mpu_dev, opp_max_now - 1, &cur_mhz_max);

	return cur_mhz_max / 1000000;
}

static void update_iva_opp_limit(int target_mhz)
{
	int volt_max;
	int i, ret;

	for (i = 0; i < opp_max_ceil - 1; i++) {
		if (target_mhz * 1000000 <= nominal_freqs_iva[i])
			break;
	}

	if (iva_opp_min == i + 1)
		return;

	//dev_info(iva_dev, "new IVA OPP %d for clock %d\n",
	//	i + 1, target_mhz);

	volt_max = iva_voltages[opp_max_avail - 1];
	volt_max += volt_max * 4 / 100;

	ret = regulator_set_voltage(iva_reg, iva_voltages[i], volt_max);
	if (ret < 0)
		dev_warn(iva_dev, "unable to set IVA OPP limits: %d\n", ret);
	else
		iva_opp_min = i + 1;
}

static int set_dsp_mhz_max(unsigned long new_mhz_max)
{
	int ret;

	mutex_lock(&iva_lock);

	if (iva_active && new_mhz_max > iva_mhz_max)
		/* going up.. */
		update_iva_opp_limit(new_mhz_max);

	ret = clk_set_rate(iva_clk, new_mhz_max * 1000000);
	if (ret != 0) {
		dev_warn(iva_dev, "unable to change IVA clock to %lu: %d\n",
			new_mhz_max, ret);
		goto out;
	}

	if (iva_active && new_mhz_max < iva_mhz_max)
		/* going down.. */
		update_iva_opp_limit(new_mhz_max);

	iva_mhz_max = new_mhz_max;
out:
	mutex_unlock(&iva_lock);

	return ret;
}

static int get_dsp_mhz_max(void)
{
	return iva_mhz_max;
}

static void iva_unneeded_work(struct work_struct *work)
{
	mutex_lock(&iva_lock);

	update_iva_opp_limit(0);
	iva_active = 0;

	mutex_unlock(&iva_lock);
}

/* called from c64_tools */
void dsp_power_notify(int enable)
{
	if (enable) {
		cancel_delayed_work_sync(&iva_work);

		mutex_lock(&iva_lock);

		if (iva_active) {
			mutex_unlock(&iva_lock);
			return;
		}

		/* apply the OPP limit */
		update_iva_opp_limit(iva_mhz_max);
		iva_active = 1;

		mutex_unlock(&iva_lock);
	}
	else {
		if (!iva_active)
			return;

		cancel_delayed_work_sync(&iva_work);
		schedule_delayed_work(&iva_work, HZ * 2);
	}
}
EXPORT_SYMBOL(dsp_power_notify);

static int init_opp_hacks(void)
{
	int iva_init_freq;
	struct opp *opp;
	int i, ret;

	if (cpu_is_omap3630()) {
		nominal_freqs_mpu = nominal_f_mpu_36xx;
		nominal_freqs_iva = nominal_f_iva_36xx;
		opp_max_avail = sizeof(nominal_f_mpu_36xx) / sizeof(nominal_f_mpu_36xx[0]);
		opp_max_ceil = 2;
	} else if (cpu_is_omap34xx()) {
		nominal_freqs_mpu = nominal_f_mpu_35xx;
		nominal_freqs_iva = nominal_f_iva_35xx;
		opp_max_avail = sizeof(nominal_f_mpu_35xx) / sizeof(nominal_f_mpu_35xx[0]);
		opp_max_ceil = opp_max_avail;
	} else {
		dev_err(mpu_dev, "%s: unsupported CPU\n", __func__);
		return -ENODEV;
	}
	opp_max_now = opp_max_ceil;

	for (i = 0; i < opp_max_avail; i++) {
		/* enable all OPPs for MPU so that cpufreq can find out
		 * maximum voltage to supply to regulator as max */
		ret = opp_enable_i(mpu_dev, i);
		if (ret != 0) {
			dev_err(mpu_dev, "opp_enable returned %d\n", ret);
			return ret;
		}

		ret = opp_enable_i(iva_dev, i);
		if (ret != 0) {
			dev_err(iva_dev, "opp_enable returned %d\n", ret);
			return ret;
		}

		opp = opp_find_freq_exact(iva_dev, nominal_freqs_iva[i], true);
		if (IS_ERR(opp)) {
			dev_err(iva_dev, "mising opp %d, %lu\n",
				i, nominal_freqs_iva[i]);
			return PTR_ERR(opp);
		}
		iva_voltages[i] = opp_get_voltage(opp);
	}

	iva_init_freq = nominal_freqs_iva[(i + 1) / 2];
	ret = clk_set_rate(iva_clk, iva_init_freq);
	if (ret == 0) {
		iva_mhz_max = iva_init_freq / 1000000;
		dev_info(iva_dev, "IVA freq set to %dMHz\n", iva_mhz_max);
	}
	else
		dev_err(iva_dev, "IVA freq set failed: %d\n", ret);

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
	return proc_read_val(page, start, off, count, eof, get_mpu_mhz_max());
}

static int cpu_clk_write(struct file *file, const char __user *buffer,
		unsigned long count, void *data)
{
	unsigned long val;
	int ret, retval;

	retval = proc_write_val(file, buffer, count, &val);
	if (retval < 0)
		return retval;

	ret = set_mpu_mhz_max(val);
	if (ret < 0)
		return ret;

	return retval;
}

static int dsp_clk_read(char *page, char **start, off_t off, int count,
		int *eof, void *data)
{
	return proc_read_val(page, start, off, count, eof, get_dsp_mhz_max());
}

static int dsp_clk_write(struct file *file, const char __user *buffer,
		unsigned long count, void *data)
{
	unsigned long val;
	int ret, retval;

	retval = proc_write_val(file, buffer, count, &val);
	if (retval < 0)
		return retval;

	ret = set_dsp_mhz_max(val);
	if (ret < 0)
		return ret;

	return retval;
}

static int cpu_maxopp_read(char *page, char **start, off_t off, int count,
		int *eof, void *data)
{
	return proc_read_val(page, start, off, count, eof, opp_max_ceil);
}

static int cpu_maxopp_write(struct file *file, const char __user *buffer,
		unsigned long count, void *data)
{
	unsigned long val;
	int ret, retval;

	retval = proc_write_val(file, buffer, count, &val);
	if (retval < 0)
		return retval;

	if (val > opp_max_avail)
		val = opp_max_avail;

	if (val < 1)
		return -EINVAL;

	ret = set_opp_max_ceil(val);
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

	INIT_DELAYED_WORK(&iva_work, iva_unneeded_work);

	mpu_dev = omap_device_get_by_hwmod_name("mpu");
	if (IS_ERR(mpu_dev)) {
		pr_err("%s: mpu device not available (%ld)\n",
			__func__, PTR_ERR(mpu_dev));
		return -ENODEV;
	}

	iva_dev = omap_device_get_by_hwmod_name("iva");
	if (IS_ERR(iva_dev)) {
		pr_err("%s: iva device not available (%ld)\n",
			__func__, PTR_ERR(iva_dev));
		return -ENODEV;
	}

	/* regulator to constrain OPPs while DSP is running */
	iva_reg = regulator_get(iva_dev, "vcc");
	if (IS_ERR(iva_reg)) {
		dev_err(iva_dev, "unable to get MPU regulator\n");
		return -ENODEV;
	}

	/* 
	 * Ensure physical regulator is present.
	 * (e.g. could be dummy regulator.)
	 */
	if (regulator_get_voltage(iva_reg) < 0) {
		dev_err(iva_dev, "IVA regulator is not physical?\n");
		ret = -ENODEV;
		goto fail_reg;
	}

	iva_clk = clk_get(NULL, "dpll2_ck");
	if (IS_ERR(iva_clk)) {
		dev_err(iva_dev, "IVA clock not available.\n");
		ret = PTR_ERR(iva_clk);
		goto fail_reg;
	}

	ret = init_opp_hacks();
	if (ret != 0) {
		pr_err("init_opp_hacks failed: %d\n", ret);
		goto fail_opp;
	}

	proc_create_rw(PROC_CPUMHZ, NULL, cpu_clk_read, cpu_clk_write);
	proc_create_rw(PROC_DSPMHZ, NULL, dsp_clk_read, dsp_clk_write);
	proc_create_rw(PROC_CPUOPP, NULL, cpu_maxopp_read, cpu_maxopp_write);
	proc_create_rw(PROC_SYSMHZ, NULL, sys_clk_read, sys_clk_write);

	pr_info("OMAP overclocker loaded.\n");
	return 0;

fail_opp:
	clk_put(iva_clk);
fail_reg:
	regulator_put(iva_reg);
	return ret;
}


static void pndctrl_cleanup(void)
{
	remove_proc_entry(PROC_SYSMHZ, NULL);
	remove_proc_entry(PROC_CPUOPP, NULL);
	remove_proc_entry(PROC_DSPMHZ, NULL);
	remove_proc_entry(PROC_CPUMHZ, NULL);

	cancel_delayed_work_sync(&iva_work);
	regulator_put(iva_reg);
	clk_put(iva_clk);
}

module_init(pndctrl_init);
module_exit(pndctrl_cleanup);

MODULE_AUTHOR("Gražvydas Ignotas");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("OMAP overclocking support");
