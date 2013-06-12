/*
 * This file is part of the libpayload project.
 *
 * Copyright (C) 2013 secunet Security Networks AG
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

//#define USB_DEBUG

#include <usb/usb.h>
#include "generic_hub.h"

/* assume that host_to_device is overwritten if necessary */
#define DR_PORT gen_bmRequestType(host_to_device, class_type, other_recp)
/* status (and status change) bits */
#define PORT_CONNECTION		0x01
#define PORT_ENABLE		0x02
#define PORT_SUSPEND		0x04
#define PORT_OVER_CURRENT	0x08
#define PORT_RESET		0x10
/* feature selectors (for setting / clearing features) */
#define SEL_PORT_RESET		0x04
#define SEL_PORT_POWER		0x08
#define SEL_C_PORT_CONNECTION	0x10
#define SEL_C_PORT_ENABLE	0x11
#define SEL_C_PORT_SUSPEND	0x12
#define SEL_C_PORT_OVER_CURRENT	0x13
#define SEL_C_PORT_RESET	0x14
/* request type (USB 3.0 hubs only) */
#define SET_HUB_DEPTH 12

static endpoint_t *
usb_hub_interrupt_ep(usbdev_t *const dev)
{
	int i;
	for (i = 0; i < dev->num_endp; ++i) {
		if (dev->endpoints[i].type == INTERRUPT &&
				dev->endpoints[i].direction == IN)
			return &dev->endpoints[i];
	}
	return NULL;
}

static int
usb_hub_port_status_changed(usbdev_t *const dev, const int port)
{
	unsigned short buf[2] = { 0, 0 };
	get_status(dev, port, DR_PORT, 4, buf);
	if (buf[1] & PORT_CONNECTION)
		clear_feature(dev, port, SEL_C_PORT_CONNECTION, DR_PORT);
	return buf[1] & PORT_CONNECTION;
}

static int
usb_hub_port_connected(usbdev_t *const dev, const int port)
{
	unsigned short buf[2] = { 0, 0 };
	get_status(dev, port, DR_PORT, 4, buf);
	return buf[0] & PORT_CONNECTION;
}

static int
usb_hub_port_in_reset(usbdev_t *const dev, const int port)
{
	unsigned short buf[2] = { 0, 0 };
	get_status(dev, port, DR_PORT, 4, buf);
	return buf[0] & PORT_RESET;
}

static int
usb_hub_port_enabled(usbdev_t *const dev, const int port)
{
	unsigned short buf[2] = { 0, 0 };
	get_status(dev, port, DR_PORT, 4, buf);
	return (buf[0] & PORT_ENABLE) != 0;
}

static usb_speed
usb_hub_port_speed(usbdev_t *const dev, const int port)
{
	unsigned short buf[2] = { 0, 0 };
	int ret;
	get_status(dev, port, DR_PORT, 4, buf);
	if (buf[0] & PORT_ENABLE) {
		/* SuperSpeed hubs can only have SuperSpeed devices. */
		if (dev->speed == SUPER_SPEED)
			return SUPER_SPEED;
		/* bit  10  9  (USB 2.0 port status word)
		 *      0   0  full speed
		 *      0   1  low speed
		 *      1   0  high speed
		 *      1   1  invalid
		 */
		ret = (buf[0] >> 9) & 0x3;
		if (ret != 0x3)
			return ret;
	}
	return -1;
}

static int
usb_hub_enable_port(usbdev_t *const dev, const int port)
{
	return set_feature(dev, port, SEL_PORT_POWER, DR_PORT);
}

static int
usb_hub_start_port_reset(usbdev_t *const dev, const int port)
{
	return set_feature(dev, port, SEL_PORT_RESET, DR_PORT);
}

static void usb_hub_set_hub_depth(usbdev_t *const dev)
{
	dev_req_t dr = {
		.bmRequestType = gen_bmRequestType(host_to_device,
						   class_type, dev_recp),
		.bRequest = SET_HUB_DEPTH,
		.wValue = 0,
		.wIndex = 0,
		.wLength = 0,
	};
	usbdev_t *parent = dev;
	while (parent->hub > 0) {
		parent = dev->controller->devices[parent->hub];
		dr.wValue++;
	}
	int ret = dev->controller->control(dev, OUT, sizeof(dr), &dr, 0, NULL);
	if (ret < 0)
		usb_debug("Failed SET_HUB_DEPTH(%d) on hub %d: %d\n",
			  dr.wValue, dev->address, ret);
}

static const generic_hub_ops_t usb_hub_ops = {
	.hub_status_changed	= NULL,
	.port_status_changed	= usb_hub_port_status_changed,
	.port_connected		= usb_hub_port_connected,
	.port_in_reset		= usb_hub_port_in_reset,
	.port_enabled		= usb_hub_port_enabled,
	.port_speed		= usb_hub_port_speed,
	.enable_port		= usb_hub_enable_port,
	.disable_port		= NULL,
	.start_port_reset	= usb_hub_start_port_reset,
	.reset_port		= generic_hub_resetport,
};

static int
usb_hub_handle_port_change(usbdev_t *const dev, const int port)
{
	unsigned short buf[2] = { 0, 0 };
	get_status(dev, port, DR_PORT, 4, buf);

	/*
	 * Second word holds the change bits. The interrupt transfer shows
	 * a logical and of these bits, so we have to clear them all.
	 */
	if (buf[1] & PORT_CONNECTION) {
		clear_feature(dev, port, SEL_C_PORT_CONNECTION, DR_PORT);
		usb_debug("usbhub: Port change at %d\n", port);
		const int ret = generic_hub_scanport(dev, port);
		if (ret < 0)
			return ret;
	}
	if (buf[1] & PORT_ENABLE)
		clear_feature(dev, port, SEL_C_PORT_ENABLE, DR_PORT);
	if (buf[1] & PORT_SUSPEND)
		clear_feature(dev, port, SEL_C_PORT_SUSPEND, DR_PORT);
	if (buf[1] & PORT_OVER_CURRENT)
		clear_feature(dev, port, SEL_C_PORT_OVER_CURRENT, DR_PORT);
	if (buf[1] & PORT_RESET)
		clear_feature(dev, port, SEL_C_PORT_RESET, DR_PORT);
	if (buf[1] & ~(PORT_CONNECTION | PORT_ENABLE |
			PORT_SUSPEND | PORT_OVER_CURRENT | PORT_RESET))
		usb_debug("usbhub: Spurious change bit at port %d\n", port);
	return 0;
}

static void
usb_hub_poll(usbdev_t *const dev)
{
	int port;
	const u8 *buf;
	while ((buf = dev->controller->poll_intr_queue(GEN_HUB(dev)->data))) {
		for (port = 1; port <= GEN_HUB(dev)->num_ports; ++port) {
			/* ports start at bit1; bit0 is hub status change */
			if (buf[port / 8] & (1 << (port % 8))) {
				if (usb_hub_handle_port_change(dev, port) < 0)
					return;
			}
		}
	}
}

static void
usb_hub_destroy(usbdev_t *const dev)
{
	endpoint_t *const intr_ep = usb_hub_interrupt_ep(dev);
	dev->controller->destroy_intr_queue(intr_ep, GEN_HUB(dev)->data);
	generic_hub_destroy(dev);
}

void
usb_hub_init(usbdev_t *const dev)
{
	int type = dev->speed == SUPER_SPEED ? 0x2a : 0x29; /* similar enough */
	hub_descriptor_t desc;	/* won't fit the whole thing, we don't care */
	if (get_descriptor(dev, gen_bmRequestType(device_to_host, class_type,
		dev_recp), type, 0, &desc, sizeof(desc)) != sizeof(desc)) {
		usb_debug("get_descriptor(HUB) failed\n");
		usb_detach_device(dev->controller, dev->address);
		return;
	}

	/* Find interrupt endpoint */
	endpoint_t *const intr_ep = usb_hub_interrupt_ep(dev);
	if (!intr_ep) {
		usb_debug("usbhub: ERROR: No interrupt-in endpoint found\n");
		return;
	}

	if (dev->speed == SUPER_SPEED)
		usb_hub_set_hub_depth(dev);
	/*
	 * Register interrupt transfer:
	 *   one bit per port + one bit for the hub),
	 *   20 transfers in the queue, like our HID driver,
	 *   one transfer per 256ms
	 */
	void *const intrq = dev->controller->create_intr_queue(
				intr_ep, (desc.bNbrPorts + 8) / 8, 20, 256);
	if (!intrq)
		return;

	/* Initialize generic hub driver */
	if (generic_hub_init(dev, desc.bNbrPorts, &usb_hub_ops)) {
		dev->controller->destroy_intr_queue(intr_ep, intrq);
		return;
	}
	GEN_HUB(dev)->data = intrq;
	/* Override poll function */
	dev->poll = usb_hub_poll;
	dev->destroy = usb_hub_destroy;
}
