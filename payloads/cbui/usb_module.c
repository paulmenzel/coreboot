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
#include <usb/usb.h>
#include "cbui.h"

#if IS_ENABLED(CONFIG_MODULE_USB) && IS_ENABLED(CONFIG_LP_USB)

/* libpayload/drivers/usb/usb.c */
extern hci_t *usb_hcs;

static int usb_module_redraw(struct nk_context *ctx)
{
	device_descriptor_t *descriptor;
	unsigned int bus, slot, func;
	hci_t *controller;
	char *name, buf[128];
	int i;

	nk_layout_row_dynamic(ctx, get_textline_height(), 1);
	nk_label(ctx, "USB Device List", NK_TEXT_ALIGN_LEFT);

	if (usb_hcs == 0) {
		nk_label(ctx, "No USB controller found.", NK_TEXT_ALIGN_LEFT);
		return;
	}

	controller = usb_hcs;

	while (controller != NULL) {
		bus = PCI_BUS(controller->pcidev);
		slot = PCI_SLOT(controller->pcidev);
		func = PCI_FUNC(controller->pcidev);

		switch(controller->type) {
		case OHCI:
			name = "OHCI";
			break;
		case UHCI:
			name = "UHCI";
			break;
		case EHCI:
			name = "EHCI";
			break;
		case XHCI:
			name = "XHCI";
			break;
		case DWC2:
			name = "DWC2";
			break;
		default:
			name = "Unknown";
			break;
		}

		snprintf(buf, 128, "USB %s controller @ PCI%04x:%02x:%02x", name, bus, slot, func);

		if (nk_tree_push_id(ctx, NK_TREE_TAB, buf, NK_MINIMIZED, item)) {
			nk_layout_row_dynamic(ctx, get_textline_height(), 1);
			sprintf(buf, "Register base %p", controller->reg_base);
			nk_label(ctx, buf, NK_TEXT_ALIGN_LEFT);
			nk_label(ctx, "Devices:", NK_TEXT_ALIGN_LEFT);

			nk_layout_row_dynamic(ctx, get_textline_height(), 4);
			nk_label(ctx, "idx", NK_TEXT_ALIGN_LEFT);
			nk_label(ctx, "Hub", NK_TEXT_ALIGN_LEFT);
			nk_label(ctx, "Address", NK_TEXT_ALIGN_LEFT);
			nk_label(ctx, "Speed", NK_TEXT_ALIGN_LEFT);
			nk_label(ctx, "Vendor ID", NK_TEXT_ALIGN_LEFT);
			nk_label(ctx, "Product ID", NK_TEXT_ALIGN_LEFT);
			nk_label(ctx, "Device class", NK_TEXT_ALIGN_LEFT);
			nk_label(ctx, "USB Version", NK_TEXT_ALIGN_LEFT);
			nk_label(ctx, "Max. packet size", NK_TEXT_ALIGN_LEFT);

			for (i = 0; i < 128; i++) {
				if (!controller->devices[i])
					continue;

				nk_value_int(ctx, "", controller->devices[i]->hub);
				nk_value_int(ctx, "", controller->devices[i]->address);
				switch(controller->devices[i]->speed) {
				case FULL_SPEED:
					nk_label(ctx, "FULL", NK_TEXT_ALIGN_LEFT);
					break;
				case LOW_SPEED:
					nk_label(ctx, "LOW", NK_TEXT_ALIGN_LEFT);
					break;
				case HIGH_SPEED:
					nk_label(ctx, "HIGH", NK_TEXT_ALIGN_LEFT);
					break;
				case SUPER_SPEED:
					nk_label(ctx, "SUPER", NK_TEXT_ALIGN_LEFT);
					break;
				default:
					nk_label(ctx, "unknown", NK_TEXT_ALIGN_LEFT);
				}

				descriptor = controller->devices[i]->descriptor;
				if (descriptor) {
					snprintf(buf, 128, "0x%04x", descriptor->idVendor);
					nk_label(ctx, buf, NK_TEXT_ALIGN_LEFT);
					snprintf(buf, 128, "0x%04x", descriptor->idProduct);
					nk_label(ctx, buf, NK_TEXT_ALIGN_LEFT);
					snprintf(buf, 128, "0x%02x", descriptor->bDeviceClass);
					nk_label(ctx, buf, NK_TEXT_ALIGN_LEFT);
					snprintf(buf, 128, "USB %02x.%02x",
						descriptor->bcdUSB >> 8, descriptor->bcdUSB & 0xff);
					nk_label(ctx, buf, NK_TEXT_ALIGN_LEFT);
					snprintf(buf, 128, "0x%02x", descriptor->bMaxPacketSize0);
					nk_label(ctx, buf, NK_TEXT_ALIGN_LEFT);
				} else {
					nk_label(ctx, "", NK_TEXT_ALIGN_LEFT);
					nk_label(ctx, "", NK_TEXT_ALIGN_LEFT);
					nk_label(ctx, "", NK_TEXT_ALIGN_LEFT);
					nk_label(ctx, "", NK_TEXT_ALIGN_LEFT);
					nk_label(ctx, "", NK_TEXT_ALIGN_LEFT);
				}
			}
			nk_tree_pop(ctx);

		}
		controller = controller->next;
	}
	return 0;
}

static void usb_scan_bus(int bus)
{

}

static int usb_module_handle(int key)
{
	return 0;
}

static int usb_module_init(void)
{
	return 0;
}

struct cbui_module usb_module = {
	.name = "USB",
	.init = usb_module_init,
	.redraw = usb_module_redraw,
	.handle = usb_module_handle,
};

#else

struct cbui_module usb_module = {
};

#endif
