/*
 * This file is part of the coreinfo project.
 *
 * Copyright (C) 2008 Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <arch/io.h>
#include <pci.h>
#include <libpayload.h>
#include "cbui.h"

#if IS_ENABLED(CONFIG_MODULE_DEBUG)

static struct cb_framebuffer *fbinfo;

static int fps;
static int get_fps(void)
{
	static int fpscnt;
	static uint64_t last_val;

	fpscnt ++;
	if (!last_val) {
		last_val = timer_raw_value();
	} else if ((timer_raw_value() - last_val) > timer_hz()) {
		fps = fpscnt;
		fpscnt = 0;
		last_val = timer_raw_value();
	}

	return fps;
}

static int updates;
static int get_updates(void)
{
	static int updatecnt;
	static uint64_t last_val;

	updatecnt ++;
	if (!last_val) {
		last_val = timer_raw_value();
	} else if ((timer_raw_value() - last_val) > timer_hz()) {
		updates = updatecnt;
		updatecnt = 0;
		last_val = timer_raw_value();
	}

	return updates;
}


static int debug_module_redraw(struct nk_context *ctx)
{
	nk_layout_row_dynamic(ctx, 200, 2);
	if (nk_group_begin(ctx, "Framebuffer", NK_WINDOW_BORDER|NK_WINDOW_NO_SCROLLBAR|NK_WINDOW_TITLE))
	{
		nk_layout_row_dynamic(ctx, get_textline_height(), 1);

		if (fbinfo) {
			nk_value_int(ctx, "Width", fbinfo->x_resolution);
			nk_value_int(ctx, "Height", fbinfo->y_resolution);
			nk_value_int(ctx, "Bpp", fbinfo->bits_per_pixel);
			nk_value_int(ctx, "FPS", get_fps());
		}

		nk_group_end(ctx);
	}
	if (nk_group_begin(ctx, "Mouse", NK_WINDOW_BORDER|NK_WINDOW_NO_SCROLLBAR|NK_WINDOW_TITLE))
	{
		nk_layout_row_dynamic(ctx, get_textline_height(), 1);

		nk_value_int(ctx, "X", mx);
		nk_value_int(ctx, "Y", my);

		nk_value_int(ctx, "Button 1", !!(mbuttons & 1));
		nk_value_int(ctx, "Button 2", !!(mbuttons & 2));
		nk_value_int(ctx, "Button 3", !!(mbuttons & 4));

		nk_group_end(ctx);
	}
	return 0;
}

static int debug_module_update()
{
	get_updates();
}

static int debug_module_init(void)
{
	/* Make sure there is a framebuffer. */
	if (lib_sysinfo.framebuffer) {
		fbinfo = (struct cb_framebuffer *)virt_to_phys(lib_sysinfo.framebuffer);
	}
	return 0;
}

struct cbui_module debug_module = {
	.name = "DEBUG",
	.init = debug_module_init,
	.redraw = debug_module_redraw,
};

#else

struct cbui_module pci_module = {
};

#endif
