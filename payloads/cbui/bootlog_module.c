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

#if IS_ENABLED(CONFIG_MODULE_BOOTLOG)

#define LINES_SHOWN 19
#define TAB_WIDTH 2

static char *buf = NULL;
static int buf_len = 0;

/* Copied from libpayload/drivers/cbmem_console.c */
struct cbmem_console {
	u32 size;
	u32 cursor;
	u8 body[0];
} __attribute__ ((__packed__));


static int bootlog_module_init(void)
{
	int i;

	/* Make sure that lib_sysinfo is initialized */
	int ret = lib_get_sysinfo();
	if (ret) {
		return -1;
	}

	struct cbmem_console *console = lib_sysinfo.cbmem_cons;
	if (console == NULL) {
		return -1;
	}
	/* Extract console information */
	char *buffer = (char *)(&(console->body));
	u32 buffer_size = console->size;
	u32 cursor = console->cursor;

	/* The cursor may be bigger than buffer size when the buffer is full */
	if (cursor >= buffer_size) {
		cursor = buffer_size - 1;
	}

	buf = malloc(buffer_size);
	if (!buf) {
		return -3;
	}

	for (i = 0; i < cursor; i++) {
		if (buffer[i] == '\n') {
			buf[i] = '\n';
		} else if (!isprint(buffer[i])) {
			buf[i] = ' ';
		} else {
			buf[i] = buffer[i];
		}
	}
	buf[cursor] = 0;
	buf_len = strlen(buf);

	return 0;
}

static int bootlog_module_redraw(struct nk_context *ctx)
{
	nk_label(ctx, "Bootlog", NK_TEXT_ALIGN_LEFT);

	if (!buf || !buf_len) {
		nk_label(ctx, "ERROR: Bootlog not found.", NK_TEXT_ALIGN_LEFT);
		return -1;
	}

	/* 30 lines in textbox with scrollbar */
    nk_layout_row_dynamic(ctx, get_textline_height() * 30, 1);
    nk_edit_string(ctx, NK_EDIT_NO_CURSOR | NK_EDIT_MULTILINE, buf, &buf_len, buf_len, NULL);

	return 0;
}

static int bootlog_module_handle(int key)
{
	return 0;
}

struct cbui_module bootlog_module = {
	.name = "Bootlog",
	.init = bootlog_module_init,
	.redraw = bootlog_module_redraw,
	.handle = bootlog_module_handle,
};

#else

struct cbui_module bootlog_module = {
};

#endif
