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

#include "cbui.h"
#include <coreboot_tables.h>

#if IS_ENABLED(CONFIG_MODULE_COREBOOT)

#define MAX_MEMORY_COUNT 5

static struct {
	int mem_count;
	int mem_actual;

	struct cb_memory_range range[MAX_MEMORY_COUNT];

	char vendor[32];
	char part[32];

	char strings[10][64];

	struct cb_serial serial;
	struct cb_console console;
} cb_info;

static int tables_good = 0;

static char cb_vendor[64];
static char cb_part[64];
static char cb_version[64];
static char cb_built[64];
static char cb_serial[64];
static char cb_console[64];
static char **cb_memorymap;

int coreboot_module_redraw(struct nk_context *ctx)
{
	int i;
	float columns;

	nk_layout_row_dynamic(ctx, get_textline_height(), 1);

	if (!tables_good) {
		nk_label(ctx, "ERROR: No Coreboot tables were found", NK_TEXT_ALIGN_LEFT);
		return 0;
	} else
		nk_label(ctx, "Coreboot Tables", NK_TEXT_ALIGN_LEFT);

	columns = 1 + MAX(4, cb_info.mem_count);
	nk_layout_row_dynamic(ctx, get_textpad_height() * columns, 3);

	nk_group_begin(ctx, "General", NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_TITLE);
	nk_layout_row_dynamic(ctx, get_textline_height(), 1);
	nk_label(ctx, cb_vendor, NK_TEXT_ALIGN_LEFT);
	nk_label(ctx, cb_part, NK_TEXT_ALIGN_LEFT);
	nk_label(ctx, cb_version, NK_TEXT_ALIGN_LEFT);
	nk_label(ctx, cb_built, NK_TEXT_ALIGN_LEFT);
	nk_group_end(ctx);

	nk_group_begin(ctx, "Serial", NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_TITLE);
	nk_layout_row_dynamic(ctx, get_textline_height(), 1);
	nk_label(ctx, cb_serial, NK_TEXT_ALIGN_LEFT);
	nk_label(ctx, cb_console, NK_TEXT_ALIGN_LEFT);
	nk_group_end(ctx);

	nk_group_begin(ctx, "Memory Map", NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_TITLE);
	nk_layout_row_dynamic(ctx, get_textline_height(), 1);
	for (i = 0; i < cb_info.mem_count; i++) {
		nk_label(ctx, cb_memorymap[i], NK_TEXT_ALIGN_LEFT);
	}
	nk_group_end(ctx);

	return 0;
}

static void parse_memory(unsigned char *ptr)
{
	struct cb_memory *mem = (struct cb_memory *)ptr;
	int max = (MEM_RANGE_COUNT(mem) > MAX_MEMORY_COUNT)
	    ? MAX_MEMORY_COUNT : MEM_RANGE_COUNT(mem);
	int i;

	for (i = 0; i < max; i++) {
		struct cb_memory_range *range =
		    (struct cb_memory_range *)MEM_RANGE_PTR(mem, i);

		memcpy(&cb_info.range[i], range, sizeof(*range));
	}

	cb_info.mem_count = max;
	cb_info.mem_actual = MEM_RANGE_COUNT(mem);
}

static void parse_mainboard(unsigned char *ptr)
{
	struct cb_mainboard *mb = (struct cb_mainboard *)ptr;

	strncpy(cb_info.vendor, cb_mb_vendor_string(mb), sizeof(cb_info.vendor) - 1);
	strncpy(cb_info.part, cb_mb_part_string(mb), sizeof(cb_info.part) - 1);
}

static void parse_strings(unsigned char *ptr)
{
	struct cb_string *string = (struct cb_string *)ptr;
	int index = string->tag - CB_TAG_VERSION;

	strncpy(cb_info.strings[index], (const char *)string->string, 63);
	cb_info.strings[index][63] = 0;
}

static void parse_serial(unsigned char *ptr)
{
	memcpy(&cb_info.serial, (struct cb_serial *)ptr,
	       sizeof(struct cb_serial));
}

static void parse_console(unsigned char *ptr)
{
	memcpy(&cb_info.console, (struct cb_console *)ptr,
	       sizeof(struct cb_console));
}

static int parse_header(void *addr, int len)
{
	struct cb_header *header;
	unsigned char *ptr = (unsigned char *)addr;
	int i;

	for (i = 0; i < len; i += 16, ptr += 16) {
		header = (struct cb_header *)ptr;

		if (!strncmp((const char *)header->signature, "LBIO", 4))
			break;
	}

	/* We walked the entire space and didn't find anything. */
	if (i >= len)
		return -1;

	if (!header->table_bytes)
		return 0;

	/* FIXME: Check the checksum. */

	if (cb_checksum(header, sizeof(*header)))
		return -1;

	if (cb_checksum((ptr + sizeof(*header)), header->table_bytes)
	    != header->table_checksum)
		return -1;

	/* Now, walk the tables. */
	ptr += header->header_bytes;

	for (i = 0; i < header->table_entries; i++) {
		struct cb_record *rec = (struct cb_record *)ptr;

		switch (rec->tag) {
		case CB_TAG_FORWARD:
			return parse_header((void *)(unsigned long)((struct cb_forward *)rec)->forward, 1);
			break;
		case CB_TAG_MEMORY:
			parse_memory(ptr);
			break;
		case CB_TAG_MAINBOARD:
			parse_mainboard(ptr);
			break;
		case CB_TAG_VERSION:
		case CB_TAG_EXTRA_VERSION:
		case CB_TAG_BUILD:
		case CB_TAG_COMPILE_TIME:
		case CB_TAG_COMPILE_BY:
		case CB_TAG_COMPILE_HOST:
		case CB_TAG_COMPILE_DOMAIN:
		case CB_TAG_COMPILER:
		case CB_TAG_LINKER:
		case CB_TAG_ASSEMBLER:
			parse_strings(ptr);
			break;
		case CB_TAG_SERIAL:
			parse_serial(ptr);
			break;
		case CB_TAG_CONSOLE:
			parse_console(ptr);
			break;
		default:
			break;
		}

		ptr += rec->size;
	}

	return 1;
}

static int coreboot_module_init(void)
{
	int i;

	tables_good = 0;

	int ret = parse_header((void *)0x00000, 0x1000);

	if (ret != 1)
		ret = parse_header((void *)0xf0000, 0x1000);

	if (ret != 1)
		return 1;

	sprintf(cb_vendor, "Vendor: %s", cb_info.vendor);
	sprintf(cb_part, "Part: %s", cb_info.part);
	sprintf(cb_version, "Version: %s%s",
		  cb_info.strings[CB_TAG_VERSION - 0x4],
		  cb_info.strings[CB_TAG_EXTRA_VERSION - 0x4]);
	sprintf(cb_built, "Built: %s (%s@%s.%s)",
		  cb_info.strings[CB_TAG_BUILD - 0x4],
		  cb_info.strings[CB_TAG_COMPILE_BY - 0x04],
		  cb_info.strings[CB_TAG_COMPILE_HOST - 0x04],
		  cb_info.strings[CB_TAG_COMPILE_DOMAIN - 0x04]);
	sprintf(cb_console, "Part: %s", cb_info.part);

	if (cb_info.serial.tag != 0x0) {
		sprintf(cb_serial, "Serial Port I/O base: 0x%x",
				  cb_info.serial.baseaddr);
	}

	if (cb_info.console.tag != 0x0) {
		switch (cb_info.console.type) {
		case CB_TAG_CONSOLE_SERIAL8250:
			sprintf(cb_console, "Default Output Console: Serial Port");
			break;
		case CB_TAG_CONSOLE_VGA:
			sprintf(cb_console, "Default Output Console: VGA");
			break;
		case CB_TAG_CONSOLE_BTEXT:
			sprintf(cb_console, "Default Output Console: BTEXT");
			break;
		case CB_TAG_CONSOLE_LOGBUF:
			sprintf(cb_console, "Default Output Console: Log Buffer");
			break;
		case CB_TAG_CONSOLE_SROM:
			sprintf(cb_console, "Default Output Console: Serial ROM");
			break;
		case CB_TAG_CONSOLE_EHCI:
			sprintf(cb_console, "Default Output Console: USB Debug");
			break;
		}
	}

	cb_memorymap = malloc(sizeof(char *) * cb_info.mem_count);
	if (!cb_memorymap)
		return 1;

	for (i = 0; i < cb_info.mem_count; i++) {
		cb_memorymap[i] = malloc(sizeof(char) * 96);
		if (!cb_memorymap[i])
			return 1;

		switch (cb_info.range[i].type) {
		case CB_MEM_RAM:
			sprintf(cb_memorymap[i], "     RAM: %16.16llx - %16.16llx",
			cb_unpack64(cb_info.range[i].start),
			cb_unpack64(cb_info.range[i].start) +
			cb_unpack64(cb_info.range[i].size) - 1);

			break;
		case CB_MEM_RESERVED:
			sprintf(cb_memorymap[i], "Reserved: %16.16llx - %16.16llx",
			cb_unpack64(cb_info.range[i].start),
			cb_unpack64(cb_info.range[i].start) +
			cb_unpack64(cb_info.range[i].size) - 1);

			break;
		case CB_MEM_TABLE:
			sprintf(cb_memorymap[i], "   Table: %16.16llx - %16.16llx",
			cb_unpack64(cb_info.range[i].start),
			cb_unpack64(cb_info.range[i].start) +
			cb_unpack64(cb_info.range[i].size) - 1);
			break;
		}
	}

	tables_good = 1;

	return 0;
}

static void coreboot_module_terminate(void)
{
	int i;

	if (!cb_memorymap)
		return;

	for (i = 0; i < cb_info.mem_count; i++) {
		if (cb_memorymap[i])
			free(cb_memorymap[i]);
	}

	free(cb_memorymap);
}

struct cbui_module coreboot_module = {
	.name = "Coreboot",
	.init = coreboot_module_init,
	.redraw = coreboot_module_redraw,
	.terminate = coreboot_module_terminate,
};

#else

struct cbui_module coreboot_module = {
};

#endif
