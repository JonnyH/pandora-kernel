/*
 * LCD panel support for OMAP3 Pandora
 *
 * Derived from drivers/video/omap/lcd_omap3evm.c
 * Derived from drivers/video/omap/lcd-apollon.c
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/i2c/twl4030.h>
#include <linux/omapfb.h>

#include <mach/gpio.h>
#include <mach/mux.h>
#include <asm/mach-types.h>

//#define LCD_PANEL_ENABLE_GPIO	154
//#define TWL_PWMA_PWMAOFF	0x01

#define LCD_XRES		800
#define LCD_YRES		480
#define LCD_PIXCLOCK		36000 /* in kHz */

static unsigned int bklight_level;

static int omap3pandora_panel_init(struct lcd_panel *panel,
				struct omapfb_device *fbdev)
{
	bklight_level = 100;

	return 0;
}

static void omap3pandora_panel_cleanup(struct lcd_panel *panel)
{
}

static int omap3pandora_panel_enable(struct lcd_panel *panel)
{
//	omap_set_gpio_dataout(LCD_PANEL_ENABLE_GPIO, 0);
	return 0;
}

static void omap3pandora_panel_disable(struct lcd_panel *panel)
{
//	omap_set_gpio_dataout(LCD_PANEL_ENABLE_GPIO, 1);
}

static unsigned long omap3pandora_panel_get_caps(struct lcd_panel *panel)
{
	return 0;
}

static int omap3pandora_bklight_setlevel(struct lcd_panel *panel,
						unsigned int level)
{
/*
	u8 c;
	if ((level >= 0) && (level <= 100)) {
		c = (125 * (100 - level)) / 100 + 2;
		twl4030_i2c_write_u8(TWL4030_MODULE_PWMA, c, TWL_PWMA_PWMAOFF);
		bklight_level = level;
	}
*/
	return 0;
}

static unsigned int omap3pandora_bklight_getlevel(struct lcd_panel *panel)
{
	return bklight_level;
}

static unsigned int omap3pandora_bklight_getmaxlevel(struct lcd_panel *panel)
{
	return 100;
}

struct lcd_panel omap3pandora_panel = {
	.name		= "omap3pandora",
	.config		= OMAP_LCDC_PANEL_TFT | OMAP_LCDC_INV_VSYNC |
			  OMAP_LCDC_INV_HSYNC | OMAP_LCDC_INV_PIX_CLOCK,

	.bpp		= 16,
	.data_lines	= 24,
	.x_res		= LCD_XRES,
	.y_res		= LCD_YRES,

	.hsw		= 1,		/* hsync_len */
	.hfp		= 68,		/* right_margin */
	.hbp		= 214,		/* left_margin */
	.vsw		= 1,		/* vsync_len */
	.vfp		= 39,		/* lower_margin */
	.vbp		= 34,		/* upper_margin */

	.acb		= 0x28,		/* ac-bias pin frequency */
	.pcd		= 0,		/* pixel clock divider. Unused */

	.pixel_clock	= LCD_PIXCLOCK,

	.init		= omap3pandora_panel_init,
	.cleanup	= omap3pandora_panel_cleanup,
	.enable		= omap3pandora_panel_enable,
	.disable	= omap3pandora_panel_disable,
	.get_caps	= omap3pandora_panel_get_caps,
	.set_bklight_level      = omap3pandora_bklight_setlevel,
	.get_bklight_level      = omap3pandora_bklight_getlevel,
	.get_bklight_max        = omap3pandora_bklight_getmaxlevel,
};

static int omap3pandora_panel_probe(struct platform_device *pdev)
{
	omapfb_register_panel(&omap3pandora_panel);
	return 0;
}

static int omap3pandora_panel_remove(struct platform_device *pdev)
{
	return 0;
}

static int omap3pandora_panel_suspend(struct platform_device *pdev,
				   pm_message_t mesg)
{
	return 0;
}

static int omap3pandora_panel_resume(struct platform_device *pdev)
{
	return 0;
}

struct platform_driver omap3pandora_panel_driver = {
	.probe		= omap3pandora_panel_probe,
	.remove		= omap3pandora_panel_remove,
	.suspend	= omap3pandora_panel_suspend,
	.resume		= omap3pandora_panel_resume,
	.driver		= {
		.name	= "pandora_lcd",
		.owner	= THIS_MODULE,
	},
};

static int __init omap3pandora_panel_drv_init(void)
{
	return platform_driver_register(&omap3pandora_panel_driver);
}

static void __exit omap3pandora_panel_drv_exit(void)
{
	platform_driver_unregister(&omap3pandora_panel_driver);
}

module_init(omap3pandora_panel_drv_init);
module_exit(omap3pandora_panel_drv_exit);
