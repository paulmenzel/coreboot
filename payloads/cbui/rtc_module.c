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

#include <libpayload.h>
#include "cbui.h"

#if IS_ENABLED(CONFIG_MODULE_RTC)

static int time_selected = 0;
static int date_selected = 0;
static struct tm sel_time;
static struct tm sel_date;

static int rtc_module_redraw(struct nk_context *ctx)
{
	float height;
	char time_str[32];
	char date_str[32];

	nk_layout_row_dynamic(ctx, get_textline_height(), 1);
	nk_label(ctx, "Change RTC", NK_TEXT_ALIGN_LEFT);

	nk_layout_row_dynamic(ctx, get_textline_height(), 2);

	height = get_textline_height() * 10.0f;

	/* time combobox */
	sprintf(time_str, "%02d:%02d:%02d", sel_time.tm_hour, sel_time.tm_min, sel_time.tm_sec);
	if (nk_combo_begin_label(ctx, time_str, nk_vec2(200, height))) {
		time_selected = 1;
		nk_layout_row_dynamic(ctx, 25, 1);
		sel_time.tm_sec = nk_propertyi(ctx, "#S:", 0, sel_time.tm_sec, 60, 1, 1);
		sel_time.tm_min = nk_propertyi(ctx, "#M:", 0, sel_time.tm_min, 60, 1, 1);
		sel_time.tm_hour = nk_propertyi(ctx, "#H:", 0, sel_time.tm_hour, 23, 1, 1);
		nk_combo_end(ctx);
	} else if (time_selected) {
		time_selected = 0;
		rtc_write_clock(&sel_time);
	}

	height = get_textline_height() * 20;

	/* date combobox */
	sprintf(date_str, "%02d-%02d-%04d", sel_date.tm_mday, sel_date.tm_mon+1, sel_date.tm_year+1900);
	if (nk_combo_begin_label(ctx, date_str, nk_vec2(200, height))) {
		const int month_days[] = {31,28,31,30,31,30,31,31,30,31,30,31};

		date_selected = 1;
		nk_layout_row_dynamic(ctx, 25, 1);
		sel_date.tm_mday = nk_propertyi(ctx, "#D:", 1, sel_date.tm_mday, month_days[sel_date.tm_mon] + 1, 1, 1) - 1;
		sel_date.tm_mon = nk_propertyi(ctx, "#M:", 1, sel_date.tm_mon + 1, 13, 1, 1) - 1;
		sel_date.tm_year = nk_propertyi(ctx, "#Y:", 2017, 1900 + sel_date.tm_year, 2100, 1, 1) - 1900;
		nk_combo_end(ctx);
	} else if (date_selected) {
		/* Update seconds... */
		sel_date.tm_sec = sel_time.tm_sec;
		sel_date.tm_min = sel_time.tm_min;
		sel_date.tm_hour = sel_time.tm_hour;
		date_selected = 0;
		rtc_write_clock(&sel_time);
	}

	return 0;
}

static int rtc_module_handle(int key)
{
	return 0;
}

static int rtc_module_init(void)
{
	return 0;
}

static int rtc_module_update(void)
{
	static struct tm old;
	struct tm tm;

	if (nvram_updating())
		return 0;

	rtc_read_clock(&tm);
	if (tm.tm_sec != old.tm_sec) {
		if (!time_selected || !date_selected) {
			/* keep time and date updated if nothing is selected */
			old.tm_sec = tm.tm_sec;

			if (!time_selected)
				memcpy(&sel_time, &tm, sizeof(struct tm));
			if (!date_selected)
				memcpy(&sel_date, &tm, sizeof(struct tm));
		}
		return 1;
	}

	return 0;
}
struct cbui_module rtc_module = {
	.name = "RTC",
	.init = rtc_module_init,
	.redraw = rtc_module_redraw,
	.handle = rtc_module_handle,
	.update = rtc_module_update,
};

#else

struct cbui_module rtc_module = {
};

#endif
