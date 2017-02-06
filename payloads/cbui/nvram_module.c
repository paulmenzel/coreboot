/*
 * This file is part of the coreinfo project.
 *
 * Copyright (C) 2008 Uwe Hermann <uwe@hermann-uwe.de>
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

#include "cbui.h"

#if IS_ENABLED(CONFIG_MODULE_NVRAM)

static char *lines[16];

static int nvram_module_redraw(struct nk_context *ctx)
{
	int i;

	nk_layout_row_dynamic(ctx, get_textline_height(), 1);
	nk_label(ctx, "NVRAM Dump", NK_TEXT_ALIGN_LEFT);

	nk_layout_row_dynamic(ctx, get_textline_height(), 1);
	nk_label(ctx, "     00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F", NK_TEXT_ALIGN_LEFT);
	nk_label(ctx, "", NK_TEXT_ALIGN_LEFT);

	for (i = 0; i < 16; i++) {
		if (lines[i])
			nk_label_wrap(ctx, lines[i]);
	}

	return 0;
}

static int nvram_module_init(void)
{
	int i, j;
	char *ptr;

	for (i = 0; i < 16; i++) {
		lines[i] = malloc(3 * 16 + 6);
		if (!lines[i])
			return -1;

		ptr = lines[i];
		ptr += sprintf(ptr, "%02X   ", i * 16);

		for (j = 0; j < 16; j++) {
			ptr += sprintf(ptr, "%02X ", nvram_read(i * 16 + j));
		}
		*ptr = 0;
	}

	return 0;
}

struct cbui_module nvram_module = {
	.name = "NVRAM",
	.init = nvram_module_init,
	.redraw = nvram_module_redraw,
};

#else

struct cbui_module nvram_module = {
};

#endif
