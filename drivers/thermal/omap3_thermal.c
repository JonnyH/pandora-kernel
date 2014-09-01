/*
 * OMAP3/DM3730 band gap thermal driver.
 *
 * Copyright (C) 2014 Grazvydas Ignotas
 * based on SPEAr Thermal Sensor driver (spear_thermal.c)
 * Copyright (C) 2011-2012 ST Microelectronics
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/thermal.h>
#include <linux/pm_runtime.h>
#include <plat/cpu.h>

#define ADC_CODE_MASK 0x7f

struct omap3_thermal_dev {
	struct device *dev;
	void __iomem *thermal_base;
	const int *adc_to_temp;
	u32 bgap_soc_mask;
	u32 bgap_eocz_mask;
};

static const int omap3630_adc_to_temp[128] = {
	-40000, -40000, -40000, -40000, -40000, -40000, -40000, -40000, // 7
	-40000, -40000, -40000, -40000, -40000, -39000, -36500, -34500, // 15
	-33000, -31000, -29000, -27000, -25000, -23000, -21000, -19250, // 23
	-17750, -16000, -14250, -12750, -11000,  -9000,  -7250,  -5750, // 31
	 -4250,  -2500,   -750,   1000,   2750,   4250,   5750,   7500, // 39
	  9250,  11000,  12750,  14250,  16000,  18000,  20000,  22000, // 47
	 24000,  26000,  27750,  29250,  31000,  32750,  34250,  36000, // 55
	 37750,  39250,  41000,  42750,  44250,  46000,  47750,  49250, // 63
	 51000,  52750,  54250,    560,  57750,  59250,  61000,  63000, // 71
	 65000,  67000,  69000,  70750,  72500,  74250,  76000,  77750, // 79
	 79250,  81000,  82750,  84250,  86000,  87750,  89250,  91000, // 87
	 92750,  94250,  96000,  97750,  99250, 101000, 102750, 104250, // 95
	106000, 108000, 110000, 112000, 114000, 116000, 117750, 119250, // 103
	121000, 122750, 124025, 125000, 125000, 125000, 125000, 125000, // 111
	125000, 125000, 125000, 125000, 125000, 125000, 125000, 125000, // 119
	125000, 125000, 125000, 125000, 125000, 125000, 125000, 125000  // 127
};

static const int omap3530_adc_to_temp[128] = {
	-40000, -40000, -40000, -40000, -40000, -39500, -38200, -36800, // 7
	-34700, -32500, -31100, -29700, -28200, -26800, -25400, -24000, // 15
	-22600, -21200, -19800, -18400, -17000, -15600, -14100, -12700, // 23
	-11300,  -9900,  -8500,  -7100,  -5700,  -4250,  -2800,  -1400, // 31
	    50,   1550,   3000,   4400,   5850,   7300,   8700,  10100, // 39
	 11550,  13000,  14400,  15800,  17200,  18850,  20100,  21500, // 47
	 22900,  24350,  25800,  27200,  28600,  30000,  31400,  32800, // 55
	 34200,  35650,  37100,  38500,  39900,  41300,  42700,  44150, // 63
	 45600,  47000,  48400,  49800,  51300,  52600,  53950,  55300, // 71
	 56700,  58100,  59500,  60900,  62300,  63700,  70050,  66400, // 79
	 67800,  69200,  70600,  72000,  73400,  74800,  76200,  77600, // 87
	 79000,  80400,  81700,  83050,  84500,  85850,  87200,  88600, // 95
	 89950,  91300,  92700,  94050,  95400,  96800,  98200,  99550, // 103
	100900, 102300, 103650, 105000, 106400, 107800, 109150, 110500, // 111
	111900, 113300, 114650, 116000, 117400, 118750, 120100, 121500, // 119
	122850, 124200, 124950, 125000, 125000, 125000, 125000, 125000  // 127
};

static int omap3_thermal_get_temp(struct thermal_zone_device *thermal,
				  unsigned long *temp)
{
	struct omap3_thermal_dev *tdev = thermal->devdata;
	int timeout;
	u32 val;
	int ret;

	ret = pm_runtime_get_sync(tdev->dev);
	if (ret < 0) {
		dev_err(tdev->dev, "pm_runtime_get_sync failed: %d\n", ret);
		return ret;
	}

	val = readl(tdev->thermal_base);
	val |= tdev->bgap_soc_mask; /* start of conversion */

	writel(val, tdev->thermal_base);
	usleep_range(428, 1000); /* at least 14 32k cycles */

	val &= ~tdev->bgap_soc_mask;
	writel(val, tdev->thermal_base);

	usleep_range(1221, 2000); /* at least 36+4 32k cycles */
	for (timeout = 1000; timeout > 0; timeout--) {
		val = readl(tdev->thermal_base);
		if (!(val & tdev->bgap_eocz_mask))
			break;
		cpu_relax();
	}

	pm_runtime_mark_last_busy(tdev->dev);
	ret = pm_runtime_put_autosuspend(tdev->dev);

	if (timeout == 0)
		dev_err(tdev->dev, "timeout waiting for eocz\n");

	*temp = tdev->adc_to_temp[val & ADC_CODE_MASK];
	return 0;
}

static const struct thermal_zone_device_ops omap3_thermal_ops = {
	.get_temp = omap3_thermal_get_temp,
};

static int omap3_thermal_probe(struct platform_device *pdev)
{
	struct thermal_zone_device *omap3_thermal = NULL;
	struct omap3_thermal_dev *tdev;
	int ret = 0;
	struct resource *stres = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!stres) {
		dev_err(&pdev->dev, "memory resource missing\n");
		return -ENODEV;
	}

	tdev = devm_kzalloc(&pdev->dev, sizeof(*tdev), GFP_KERNEL);
	if (!tdev)
		return -ENOMEM;

	tdev->dev = &pdev->dev;

	if (cpu_is_omap3630()) {
		tdev->bgap_soc_mask = BIT(9);
		tdev->bgap_eocz_mask = BIT(8);
		tdev->adc_to_temp = omap3630_adc_to_temp;
	} else if (cpu_is_omap34xx()) {
		tdev->bgap_soc_mask = BIT(8);
		tdev->bgap_eocz_mask = BIT(7);
		tdev->adc_to_temp = omap3530_adc_to_temp;
	} else {
		dev_err(&pdev->dev, "not OMAP3 family\n");
		return -ENODEV;
	}

	tdev->thermal_base = devm_ioremap(&pdev->dev, stres->start,
			resource_size(stres));
	if (!tdev->thermal_base) {
		dev_err(&pdev->dev, "ioremap failed\n");
		return -ENOMEM;
	}

	pm_runtime_enable(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, 2000);
	pm_runtime_use_autosuspend(&pdev->dev);

	omap3_thermal = thermal_zone_device_register("omap3-thermal", 0,
				tdev, &omap3_thermal_ops, 0, 0, 0, 0);
	if (!omap3_thermal) {
		dev_err(&pdev->dev, "thermal zone device is NULL\n");
		ret = -EINVAL;
		goto put_pm;
	}

	platform_set_drvdata(pdev, omap3_thermal);

	return 0;

put_pm:
	pm_runtime_disable(&pdev->dev);
	return ret;
}

static int omap3_thermal_exit(struct platform_device *pdev)
{
	struct thermal_zone_device *omap3_thermal = platform_get_drvdata(pdev);

	thermal_zone_device_unregister(omap3_thermal);
	platform_set_drvdata(pdev, NULL);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static struct platform_driver omap3_thermal_driver = {
	.probe = omap3_thermal_probe,
	.remove = omap3_thermal_exit,
	.driver = {
		.name = "omap3-thermal",
		.owner = THIS_MODULE,
	},
};

module_platform_driver(omap3_thermal_driver);

MODULE_AUTHOR("Grazvydas Ignotas <notasas@gmail.com>");
MODULE_DESCRIPTION("OMAP3/DM3730 thermal driver");
MODULE_LICENSE("GPL");
