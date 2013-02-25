/*
 * DRM/KMS platform data for TI OMAP platforms
 *
 * Copyright (C) 2012 Texas Instruments
 * Author: Rob Clark <rob.clark@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __PLATFORM_DATA_OMAP_DRM_H__
#define __PLATFORM_DATA_OMAP_DRM_H__

/*
 * Optional platform data to configure the default configuration of which
 * pipes/overlays/CRTCs are used.. if this is not provided, then instead the
 * first CONFIG_DRM_OMAP_NUM_CRTCS are used, and they are each connected to
 * one manager, with priority given to managers that are connected to
 * detected devices.  This should be a good default behavior for most cases,
 * but yet there still might be times when you wish to do something different.
 */
struct omap_drm_platform_data {
	int ovl_cnt;
	const int *ovl_ids;
	int mgr_cnt;
	const int *mgr_ids;
	int dev_cnt;
	const char **dev_names;
};

#endif /* __PLATFORM_DATA_OMAP_DRM_H__ */
