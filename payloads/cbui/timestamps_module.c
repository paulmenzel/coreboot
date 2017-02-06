/*
 * This file is part of the coreinfo project.
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
#include <commonlib/timestamp_serialized.h>

#if IS_ENABLED(CONFIG_MODULE_TIMESTAMPS)

#define LINES_SHOWN 19
#define TAB_WIDTH 2

/* Globals that are used for tracking screen state */
static char **lines;
static s32 lines_count;
static char *total_entries_str;
static char *total_time_str;

static unsigned long tick_freq_mhz;

static const char *timestamp_name(uint32_t id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(timestamp_ids); i++) {
		if (timestamp_ids[i].id == id)
			return timestamp_ids[i].name;
	}

	return "<unknown>";
}

static void timestamp_set_tick_freq(unsigned long table_tick_freq_mhz)
{
	tick_freq_mhz = table_tick_freq_mhz;

	/* Honor table frequency. */
	if (tick_freq_mhz)
		return;

	tick_freq_mhz = lib_sysinfo.cpu_khz / 1000;

	if (!tick_freq_mhz) {
		fprintf(stderr, "Cannot determine timestamp tick frequency.\n");
		exit(1);
	}
}

static u64 arch_convert_raw_ts_entry(u64 ts)
{
	return ts / tick_freq_mhz;
}

static u32 char_width(char c, u32 cursor, u32 screen_width)
{
	if (c == '\n')
		return screen_width - (cursor % screen_width);
	else if (c == '\t')
		return TAB_WIDTH;
	else if (isprint(c))
		return 1;

	return 0;
}

static u32 calculate_chars_count(char *str, u32 str_len, u32 screen_width,
		u32 screen_height)
{
	u32 i, count = 0;

	for (i = 0; i < str_len; i++)
		count += char_width(str[i], count, screen_width);

	/* Ensure that 'count' can occupy at least the whole screen */
	if (count < screen_width * screen_height)
		count = screen_width * screen_height;

	/* Pad to line end */
	if (count % screen_width != 0)
		count += screen_width - (count % screen_width);

	return count;
}

/*
 * This method takes an input buffer and sanitizes it for display, which means:
 *  - '\n' is converted to spaces until end of line
 *  - Tabs are converted to spaces of size TAB_WIDTH
 *  - Only printable characters are preserved
 */
static int sanitize_buffer_for_display(char *str, u32 str_len, char *out,
		u32 out_len, u32 screen_width)
{
	u32 cursor = 0;
	u32 i;

	for (i = 0; i < str_len && cursor < out_len; i++) {
		u32 width = char_width(str[i], cursor, screen_width);

		if (width == 1)
			out[cursor++] = str[i];
		else if (width > 1)
			while (width-- && cursor < out_len)
				out[cursor++] = ' ';
	}

	/* Fill the rest of the out buffer with spaces */
	while (cursor < out_len)
		out[cursor++] = ' ';

	return 0;
}

static uint64_t timestamp_print_entry(char **buffer,
		uint32_t id, uint64_t stamp, uint64_t prev_stamp)
{
	const char *name;
	uint64_t step_time;
	u32 cur = 0;

	name = timestamp_name(id);
	step_time = arch_convert_raw_ts_entry(stamp - prev_stamp);

	*buffer = malloc(96);
	if (*buffer)
	{
		cur += snprintf(*buffer + cur, 64, "%4d: %-45s", id, name);
		cur += sprintf(*buffer + cur, "%llu µs",
				arch_convert_raw_ts_entry(stamp));
		if (prev_stamp) {
			cur += sprintf(*buffer + cur, " (");
			cur += sprintf(*buffer + cur, "%llu", step_time);
			cur += sprintf(*buffer + cur, ")");
		}
	}
	return step_time;
}

static int timestamps_module_init(void)
{
	/* Make sure that lib_sysinfo is initialized */
	int ret = lib_get_sysinfo();

	if (ret)
		return -1;

	struct timestamp_table *timestamps = lib_sysinfo.tstamp_table;

	if (timestamps == NULL)
		return -1;

	/* Extract timestamps information */
	u64 base_time = timestamps->base_time;
	u32 n_entries = timestamps->num_entries;

	timestamp_set_tick_freq(timestamps->tick_freq_mhz);

	uint64_t prev_stamp;
	uint64_t total_time;

	/* Allocate a buffer big enough to contain all of the possible
	 * entries plus the other information (number entries, total time). */
	total_entries_str = malloc(32);
	if (total_entries_str == NULL)
		return -3;

	total_time_str = malloc(32);
	if (total_time_str == NULL)
		return -3;

	/* Write the content */
	sprintf(total_entries_str, "Entries total: %d ", n_entries);

	lines_count = 1 + n_entries;
	lines = malloc(lines_count * sizeof(void *));
	if (lines == NULL)
		return -3;

	prev_stamp = 0;
	timestamp_print_entry(&lines[0], 0, base_time,
			prev_stamp);
	prev_stamp = base_time;

	total_time = 0;
	for (int i = 0; i < n_entries; i++) {
		uint64_t stamp;
		const struct timestamp_entry *tse = &timestamps->entries[i];

		stamp = tse->entry_stamp + base_time;
		total_time += timestamp_print_entry(&lines[1 + i],
				tse->entry_id, stamp, prev_stamp);
		prev_stamp = stamp;
	}

	sprintf(total_time_str, "Total Time: %llu µs", total_time);

	return 0;
}

static int timestamps_module_redraw(struct nk_context *ctx)
{
	int i;

	nk_label(ctx, "Coreboot Timestamps", NK_TEXT_ALIGN_LEFT);

	if (!lines_count) {
		nk_label(ctx, "ERROR: No timestamps found.", NK_TEXT_ALIGN_LEFT);
		return -1;
	}

	nk_layout_row_dynamic(ctx, get_textline_height(), 1);

	nk_label(ctx, total_entries_str, NK_TEXT_ALIGN_LEFT);
	nk_label(ctx, total_time_str, NK_TEXT_ALIGN_LEFT);
	for (i = 0; i < lines_count; i ++) {
		if (lines[i])
			nk_label(ctx, lines[i], NK_TEXT_ALIGN_LEFT);
	}
	return 0;
}

static int timestamps_module_handle(int key)
{
	return 0;
}

struct cbui_module timestamps_module = {
	.name = "Timestamps",
	.init = timestamps_module_init,
	.redraw = timestamps_module_redraw,
	.handle = timestamps_module_handle,
};

#else

struct cbui_module timestamps_module = {
};

#endif
