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

#if IS_ENABLED(CONFIG_MODULE_CMOS)

struct cmos_entry {
	char *name;
	char config;
	unsigned char changed;
	char *option;
};

static struct cb_cmos_option_table *opttbl;
static struct cmos_entry *entries;
static int n_entries;

static void cmos_save(void)
{
	int i;
	struct cmos_entry *entry;

	for (i = 0; i < n_entries; i++) {
		char *ptr;

		entry = &entries[i];

		for (ptr = entry->option + strlen(entry->option) - 1;
		     ptr >= entry->option && *ptr == ' '; ptr--);
		ptr[1] = '\0';
		set_option_from_string(use_nvram, opttbl, entry->option, entry->name);
	}
}

static void cmos_free(void)
{
	int i;
	struct cmos_entry *entry;

	for (i = 0; i < n_entries; i++) {
		entry = &entries[i];
		if(entry->config == 's' || entry->config == 'h')
			free(entry->option);
	}
	free(entries);
	entries = NULL;
}

static int cmos_module_init(void)
{
	struct cb_cmos_entries *option;
	struct cb_cmos_enums *cmos_enum;
	int fail;
	char *buf;

	opttbl = get_system_option_table();
	if (!opttbl)
		return -1;

	option = first_cmos_entry(opttbl);

	n_entries = 0;
	while (option) {
		if ((option->config != 'r') &&
			(strcmp("check_sum", (char *)option->name) != 0)) {
			n_entries++;
		}

		option = next_cmos_entry(option);
	}
	if (!n_entries)
		return -1;

	entries = malloc(n_entries * sizeof(struct cmos_entry));
	if (!entries)
		return -3;

	n_entries = 0;
	option = first_cmos_entry(opttbl);
	while (option) {
		if ((option->config != 'r') &&
			(strcmp("check_sum", (char *)option->name) != 0)) {
			entries[n_entries].name = (char *)option->name;
			entries[n_entries].changed = 0;
			entries[n_entries].config = option->config;

			if (option->config == 'h' || option->config == 's') {
				if (!get_option_as_string(use_nvram, opttbl, &buf, (char *)option->name)) {
					entries[n_entries].option = malloc(strlen(buf) + 64);
					if (!entries[n_entries].option) {
						opttbl = NULL;
						return -3;
					}

					strcpy(entries[n_entries].option, buf);
				}
			} else if (option->config == 'e') {
				cmos_enum = first_cmos_enum_of_id(opttbl, option->config_id);

				fail = get_option_as_string(use_nvram, opttbl, &buf, (char *)option->name);
				if (cmos_enum && fail) {
					entries[n_entries].option = (char *)cmos_enum->text;
				} else if (!fail) {
					/* This will leak buf ... */
					entries[n_entries].option = buf;
				} else {
					entries[n_entries].option = "";
				}
			}

			n_entries++;
		}

		option = next_cmos_entry(option);
	}

	return 0;
}

static int cmos_module_redraw(struct nk_context *ctx)
{
	struct cb_cmos_enums *cmos_enum;
	char *buf = NULL;
	struct cmos_entry *entry;
	struct cb_cmos_entries *option;

	nk_layout_row_dynamic(ctx, get_textline_height(), 1);

	nk_label(ctx, "Modify CMOS options", NK_TEXT_LEFT);

	if (!opttbl || !entries || !n_entries) {
		nk_label(ctx, "ERROR: No CMOS options found.", NK_TEXT_LEFT);
		return -1;
	}

	nk_layout_row_dynamic(ctx, get_textline_height(), 2);

	option = first_cmos_entry(opttbl);
	for (int i = 0; i < n_entries; i++) {
		while ((option->config == 'r') ||
			   (strcmp("check_sum", (char *)option->name) == 0)) {
			option = next_cmos_entry(option);
		}

		nk_label(ctx, (const char *)option->name, NK_TEXT_LEFT);

		entry = &entries[i];

		if (option->config == 'e') {
			cmos_enum = first_cmos_enum_of_id(opttbl, option->config_id);

			if (entry && nk_combo_begin_label(ctx, entry->option, nk_vec2(nk_widget_width(ctx), 200))) {
				nk_layout_row_dynamic(ctx, get_textline_height(), 1);

				while (cmos_enum) {
					if (nk_combo_item_label(ctx, (const char *)cmos_enum->text, NK_TEXT_LEFT))
					{
						entry->changed = !strcmp((const char *)cmos_enum->text, buf);
						entry->option = (char *)cmos_enum->text;
					}
					cmos_enum = next_cmos_enum_of_id(
						cmos_enum, option->config_id);
				}

				nk_combo_end(ctx);
			}
		} else if(option->config == 's' || option->config == 'h') {
			nk_edit_string_zero_terminated(ctx, NK_TEXT_RIGHT, entry->option, 100, NULL);
			entry->changed = !strcmp((const char *)entry->option, buf);
		}

		option = next_cmos_entry(option);
	}

	nk_layout_row_dynamic(ctx, get_textline_height(), 3);

    if (nk_button_label(ctx, "Save")) {
    	cmos_save();
    }
	nk_label(ctx, "", NK_TEXT_LEFT);

    if (nk_button_label(ctx, "Discard")) {
    	cmos_free();
    	cmos_module_init();
    }

	return 0;
}


struct cbui_module cmos_module = {
	.name = "CMOS",
	.init = cmos_module_init,
	.redraw = cmos_module_redraw,
};

#else

struct cbui_module cmos_module = {
};

#endif
