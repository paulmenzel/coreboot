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

#if IS_ENABLED(CONFIG_MODULE_PCI)

struct pci_devices {
	pcidev_t device;
	unsigned int id;
	unsigned int class;
	const char* class_name;
	unsigned int header;
};

#define MAX_PCI_DEVICES 64
static struct pci_devices devices[MAX_PCI_DEVICES];
static int devices_index;

static void swap(struct pci_devices *a, struct pci_devices *b)
{
	struct pci_devices tmp;

	tmp = *a;
	*a = *b;
	*b = tmp;
}

static int partition(struct pci_devices *list, int len)
{
	int val = list[len / 2].device;
	int index = 0;
	int i;

	swap(&list[len / 2], &list[len - 1]);

	for (i = 0; i < len - 1; i++) {
		if (list[i].device < val) {
			swap(&list[i], &list[index]);
			index++;
		}
	}

	swap(&list[index], &list[len - 1]);

	return index;
}

static void quicksort(struct pci_devices *list, int len)
{
	int index;

	if (len <= 1)
		return;

	index = partition(list, len);

	quicksort(list, index);
	quicksort(&(list[index]), len - index);
}

static int pci_module_redraw(struct nk_context *ctx)
{
	unsigned int bus, slot, func;
	unsigned int vendor, device;
	unsigned int class;
	int item;
	char buf[128];

	nk_layout_row_dynamic(ctx, get_textline_height(), 1);
	nk_label(ctx, "PCI Device List", NK_TEXT_ALIGN_LEFT);

	for (item = 0; item < devices_index; item ++) {
		bus = PCI_BUS(devices[item].device);
		slot = PCI_SLOT(devices[item].device);
		func = PCI_FUNC(devices[item].device);
		vendor = devices[item].id & 0xffff;
		device = (devices[item].id >> 16) & 0xffff;
		class = devices[item].class;

		sprintf(buf, "%02X:%2.2X.%2.2X [%s]", bus, slot, func,
				devices[item].class_name);

		if (nk_tree_push_id(ctx, NK_TREE_TAB, buf, NK_MINIMIZED, item)) {
			nk_layout_row_dynamic(ctx, get_textline_height(), 1);
			sprintf(buf, "Vendor 0x%04x, Device 0x%04X",
					vendor,
					device);
			nk_label(ctx, buf, NK_TEXT_ALIGN_LEFT);

			sprintf(buf, "Classcode 0x%02x, Subclass 0x%02x, Prog IF 0x%02x, Revision ID 0x%02x",
					(class >> 24) & 0xff,
					(class >> 16) & 0xff,
					(class >>  8) & 0xff,
					(class >>  0) & 0xff);
			nk_label(ctx, buf, NK_TEXT_ALIGN_LEFT);

			sprintf(buf, "Header type 0x%02x",
			    devices[item].header);
			nk_label(ctx, buf, NK_TEXT_ALIGN_LEFT);

			nk_tree_pop(ctx);
		}
	}
	return 0;
}

static const char *class_name(unsigned int class)
{
	switch (class) {
		case 0x00: return "Uncategorized";
		case 0x01: return "Mass Storage Controller";
		case 0x02: return "Network Controller";
		case 0x03: return "Display Controller";
		case 0x04: return "Multimedia Controller";
		case 0x05: return "Memory Controller";
		case 0x06: return "Bridge Device";
		case 0x07: return "Simple Communication Controllers";
		case 0x08: return "Base System Peripherals";
		case 0x09: return "Input Devices";
		case 0x0A: return "Network Controller";
		case 0x0B: return "Processors";
		case 0x0C: return "Serial Bus Controllers";
		case 0x0D: return "Wireless Controllers";
		case 0x0E: return "Intelligent I/O Controllers";
		case 0x0F: return "Satellite Communication Controllers";
		case 0x10: return "Encryption/Decryption Controllers";
		case 0x11: return "Data Acquisition and Signal Processing Controllers";
		default:
			if (class == 0xff) return "";
			return "Reserved";
	}
}

static void pci_scan_bus(int bus)
{
	int slot, func;
	unsigned int val;
	unsigned char hdr;
	unsigned int class;

	for (slot = 0; slot < 0x20; slot++) {
		for (func = 0; func < 8; func++) {
			pcidev_t dev = PCI_DEV(bus, slot, func);

			val = pci_read_config32(dev, REG_VENDOR_ID);

			/* Nobody home. */
			if (val == 0xffffffff || val == 0x00000000 ||
			    val == 0x0000ffff || val == 0xffff0000)
				continue;

			/* FIXME: Remove this arbitrary limitation. */
			if (devices_index >= MAX_PCI_DEVICES)
				return;

			class = pci_read_config32(dev, REG_REVISION_ID);
			hdr = pci_read_config8(dev, REG_HEADER_TYPE);
			hdr &= 0x7f;

			devices[devices_index].device =
			    PCI_DEV(bus, slot, func);

			devices[devices_index].id = val;
			devices[devices_index].header = hdr;
			devices[devices_index].class = class;
			devices[devices_index].class_name =
			    class_name(class >> 24);

			devices_index++;

			/* If this is a bridge, then follow it. */
			if (hdr == HEADER_TYPE_BRIDGE ||
			    hdr == HEADER_TYPE_CARDBUS) {
				unsigned int busses;

				busses = pci_read_config32(dev, REG_PRIMARY_BUS);

				pci_scan_bus((busses >> 8) & 0xff);

			}
		}
	}

	quicksort(devices, devices_index);
}

static int pci_module_handle(int key)
{
	return 0;
}

static int pci_module_init(void)
{
	pci_scan_bus(0);
	return 0;
}

struct cbui_module pci_module = {
	.name = "PCI",
	.init = pci_module_init,
	.redraw = pci_module_redraw,
	.handle = pci_module_handle,
};

#else

struct cbui_module pci_module = {
};

#endif
