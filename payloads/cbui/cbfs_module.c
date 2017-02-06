/*
 * This file is part of the coreinfo project.
 *
 * Copyright (C) 2009 Uwe Hermann <uwe@hermann-uwe.de>
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

#define NK_INCLUDE_STANDARD_VARARGS

#include "cbui.h"
#include "endian.h"

#if IS_ENABLED(CONFIG_MODULE_CBFS)

#define FILES_VISIBLE		19

#define HEADER_MAGIC		0x4F524243
#define HEADER_ADDR		0xfffffffc
#define LARCHIVE_MAGIC		0x455649484352414cLL	/* "LARCHIVE" */

#define COMPONENT_DELETED	0x00
#define COMPONENT_BOOTBLOCK	0x01
#define COMPONENT_CBFSHEADER	0x02
#define COMPONENT_STAGE		0x10
#define COMPONENT_PAYLOAD	0x20
#define COMPONENT_OPTIONROM	0x30
#define COMPONENT_RAW		0x50
#define COMPONENT_MICROCODE	0x53
#define COMPONENT_CMOS_LAYOUT	0x1aa
#define COMPONENT_NULL		0xffffffff

struct cbheader {
	u32 magic;
	u32 version;
	u32 romsize;
	u32 bootblocksize;
	u32 align;
	u32 offset;
	u32 architecture;
	u32 pad[1];
} __attribute__ ((packed));

struct cbfile {
	u64 magic;
	u32 len;
	u32 type;
	u32 checksum;
	u32 offset;
	char filename[0];
} __attribute__ ((packed));

static int filecount = 0;
static char **filenames;
static struct cbheader *header = NULL;

static struct cbfile *getfile(struct cbfile *f)
{
	while (1) {
		if (f < (struct cbfile *)(0xffffffff - ntohl(header->romsize)))
			return NULL;
		if (f->magic == 0)
			return NULL;
		if (f->magic == LARCHIVE_MAGIC)
			return f;
		f = (void *)f + ntohl(header->align);
	}
}

static struct cbfile *firstfile(void)
{
	return getfile((void *)(0 - ntohl(header->romsize) +
				ntohl(header->offset)));
}

static struct cbfile *nextfile(struct cbfile *f)
{
	f = (void *)f + ALIGN(ntohl(f->len) + ntohl(f->offset),
			      ntohl(header->align));
	return getfile(f);
}

static struct cbfile *findfile(const char *filename)
{
	struct cbfile *f;
	for (f = firstfile(); f; f = nextfile(f)) {
		if (strcmp(filename, f->filename) == 0)
			return f;
	}
	return NULL;
}

static int cbfs_module_init(void)
{
	struct cbfile *f;
	int index = 0;

	header = *(void **)HEADER_ADDR;
	if (header->magic != ntohl(HEADER_MAGIC)) {
		header = NULL;
		return 0;
	}

	for (f = firstfile(); f; f = nextfile(f))
		filecount++;

	filenames = malloc(filecount * sizeof(char *));
	if (filenames == NULL)
		return 0;

	for (f = firstfile(); f; f = nextfile(f))
		filenames[index++] = strdup((const char *)f->filename);

	return 0;
}

static int cbfs_module_redraw(struct nk_context *ctx)
{
	struct cbfile *f;
	int i;
	char *filename;

	nk_layout_row_dynamic(ctx, get_textline_height(), 1);

	nk_label(ctx, "CBFS Listing", NK_TEXT_ALIGN_LEFT);

	if (!header) {
		nk_label(ctx, "ERROR: Bad or missing CBFS header.", NK_TEXT_ALIGN_LEFT);
		return -1;
	}

	nk_layout_row_dynamic(ctx, get_textline_height(), 4);

	nk_label(ctx, "Filename", NK_TEXT_ALIGN_LEFT);
	nk_label(ctx, "Type", NK_TEXT_ALIGN_LEFT);
	nk_label(ctx, "Size", NK_TEXT_ALIGN_LEFT);
	nk_label(ctx, "Checksum", NK_TEXT_ALIGN_LEFT);

	nk_label(ctx, "", NK_TEXT_ALIGN_LEFT);
	nk_label(ctx, "", NK_TEXT_ALIGN_LEFT);
	nk_label(ctx, "", NK_TEXT_ALIGN_LEFT);
	nk_label(ctx, "", NK_TEXT_ALIGN_LEFT);

	for (i = 0; i < filecount; i ++) {
		if (strlen(filenames[i]) == 0) {
			if (findfile(filenames[i])->type == COMPONENT_NULL)
				filename = "<free space>";
			else
				filename = "<unnamed>";
		} else {
			filename = filenames[i];
		}
		nk_label(ctx, filename, NK_TEXT_ALIGN_LEFT);

		f = findfile(filename);
		if (!f) {
			nk_label(ctx, "", NK_TEXT_ALIGN_LEFT);
			nk_label(ctx, "", NK_TEXT_ALIGN_LEFT);
			nk_label(ctx, "", NK_TEXT_ALIGN_LEFT);
		} else {
			switch (ntohl(f->type)) {
			case COMPONENT_BOOTBLOCK:
				nk_label(ctx, "bootblock", NK_TEXT_ALIGN_LEFT);
				break;
			case COMPONENT_CBFSHEADER:
				nk_label(ctx, "CBFS header", NK_TEXT_ALIGN_LEFT);
				break;
			case COMPONENT_STAGE:
				nk_label(ctx, "stage", NK_TEXT_ALIGN_LEFT);
				break;
			case COMPONENT_PAYLOAD:
				nk_label(ctx, "payload", NK_TEXT_ALIGN_LEFT);
				break;
			case COMPONENT_OPTIONROM:
				nk_label(ctx, "optionrom", NK_TEXT_ALIGN_LEFT);
				break;
			case COMPONENT_RAW:
				nk_label(ctx, "raw", NK_TEXT_ALIGN_LEFT);
				break;
			case COMPONENT_MICROCODE:
				nk_label(ctx, "microcode", NK_TEXT_ALIGN_LEFT);
				break;
			case COMPONENT_CMOS_LAYOUT:
				nk_label(ctx, "cmos layout", NK_TEXT_ALIGN_LEFT);
				break;
			case COMPONENT_NULL:
				nk_label(ctx, "free", NK_TEXT_ALIGN_LEFT);
				break;
			case COMPONENT_DELETED:
				nk_label(ctx, "deleted", NK_TEXT_ALIGN_LEFT);
				break;
			default:
				nk_labelf(ctx, NK_TEXT_ALIGN_LEFT, "Unknown (0x%x)", ntohl(f->type));
				break;
			}
			nk_labelf(ctx, NK_TEXT_ALIGN_LEFT, "%d", ntohl(f->len));
			nk_labelf(ctx, NK_TEXT_ALIGN_LEFT, "0x%x", ntohl(f->checksum));
		}
	}

	return 0;
}

static int cbfs_module_handle(int key)
{
	return 0;
}

struct cbui_module cbfs_module = {
	.name = "CBFS",
	.init = cbfs_module_init,
	.redraw = cbfs_module_redraw,
	.handle = cbfs_module_handle
};

#else

struct cbui_module cbfs_module = {
};

#endif
