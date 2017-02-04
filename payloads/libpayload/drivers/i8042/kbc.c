/*
 * This file is part of the libpayload project.
 *
 * Patrick Rudolph 2017 <siro@das-labor.org>
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

#include <libpayload-config.h>
#include <libpayload.h>
#include "kbc.h"
#include "fifo.h"

/* Keyboard controller methods */
static unsigned char initialized;
static struct fifo *aux_fifo;
static struct fifo *ps2_fifo;

/** Wait for command ready.
 * Wait for the keyboard controller to accept a new command.
 * Returns: 0 on timeout
 */
static unsigned char kbc_wait_cmd_rdy(void)
{
	int retries = 10000;
	while (retries-- && (inb(0x64) & 0x02))
		udelay(50);

	return retries > 0;
}

/** Wait for data ready.
 * Wait for the keyboard controller to accept new data.
 * Returns: 0 on timeout
 */
static unsigned char kbc_wait_data_rdy(void)
{
	int retries = 10000;
	while (retries-- && !(inb(0x64) & 0x01))
		udelay(50);

	return retries > 0;
}

/** Keyboard controller has a ps2 port.
 * Returns if ps2 port is available.
 */
unsigned char kbc_has_ps2(void)
{
	return !!ps2_fifo;
}

/** Keyboard controller has an aux port.
 * Returns if aux port is available.
 */
unsigned char kbc_has_aux(void)
{
	return !!aux_fifo;
}

/**
 * Probe for keyboard controller
 * Returns: 1 for success, 0 for failure
 */
int kbc_probe(void)
{
	if (initialized)
		return 1;

	/* If 0x64 returns 0xff, then we have no keyboard
	 * controller */
	if (inb(0x64) == 0xFF)
		return 0;

	if (!kbc_wait_cmd_rdy())
		return 0;

	/* Disable first device */
	outb(0xad, 0x64);

	if (!kbc_wait_cmd_rdy())
		return 0;

	/* Disable second device */
	outb(0xa7, 0x64);

	if (!kbc_wait_cmd_rdy())
		return 0;

	/* Flush buffer */
	while (inb(0x64) & 0x01)
		inb(0x60);

	if (!kbc_wait_cmd_rdy())
		return 0;

	/* Self test. */
	outb(0xaa, 0x64);

	if (!kbc_wait_cmd_rdy())
		return 0;

	/* Wait for answer. */
	if (!kbc_wait_data_rdy())
		return 0;

	initialized = inb(0x60) == 0x55;

	if (!kbc_wait_cmd_rdy())
		return 0;

	/* Test secondary port. */
	if (kbc_cmd(0xa9, 1) == 0)
		aux_fifo = fifo_init(4 * 32);

	/* Test first PS/2 port. */
	if (kbc_cmd(0xab, 1) == 0)
		ps2_fifo = fifo_init(2 * 16);

	return initialized;
}

/** Send command to keyboard controller.
 * @param cmd: The command to be send.
 * @param response: Wait for and return response.
 * returns: Response if any, otherwise 0 on success, -1 on failure.
 */
int kbc_cmd(unsigned char cmd, unsigned char response)
{
	if (!initialized)
		return -1;

	if (!kbc_wait_cmd_rdy())
		return -1;

	outb(cmd, 0x64);

	if (!kbc_wait_cmd_rdy())
		return -1;

	if (response) {
		if (!kbc_wait_data_rdy())
			return -1;

		return inb(0x60);
	}

	return 0;
}

/** Send additional data to keyboard controller.
 * @param data The data to be send.
 */
void kbc_write_input(unsigned char data)
{
	if (!initialized)
		return;

	if (!kbc_wait_cmd_rdy())
		return;

	outb(data, 0x60);

	if (!kbc_wait_cmd_rdy())
		return;
}

/**
 * Probe for keyboard controller data and queue it.
 */
static void kbc_data_poll(void)
{
	unsigned char c;

	if (!initialized)
		return;

	c = inb(0x64);
	while ((c != 0xFF) && (c & 1)) {
		/* Assume "second PS/2 port output buffer full" flag works ... */
		if ((c & 0x20) && aux_fifo)
			fifo_push(aux_fifo, inb(0x60));
		else if (!(c & 0x20) && ps2_fifo)
			fifo_push(ps2_fifo, inb(0x60));

		c = inb(0x64);
	}
}

/** Keyboard controller data ready status.
 * Signals that keyboard data is ready for reading.
 */
int kbc_data_ready_kb(void)
{
	kbc_data_poll();
	return !fifo_is_empty(ps2_fifo);
}

/** Keyboard controller data ready status.
 * Signals that mouse data is ready for reading.
 */
int kbc_data_ready_mo(void)
{
	kbc_data_poll();
	return !fifo_is_empty(aux_fifo);
}

/**
 * Returns available keyboard data, if any.
 */
unsigned char kbc_data_get_kb(void)
{
	kbc_data_poll();
	return fifo_pop(ps2_fifo);
}

/**
 * Returns available mouse data, if any.
 */
unsigned char kbc_data_get_mo(void)
{
	kbc_data_poll();
	return fifo_pop(aux_fifo);
}

/**
 * Waits for keyboard data.
 * Waits for up to 500msec to receive data.
 * Returns: -1 on timeout, data received otherwise
 */
int kbc_wait_read_kb(void)
{
	int retries = 10000;

	while (retries-- && !kbc_data_ready_kb())
		udelay(50);

	return (retries <= 0) ? -1 : kbc_data_get_kb();
}

/** Waits for mouse data.
 * Waits for up to 500msec to receive data.
 * Returns: -1 on timeout, data received otherwise
 */
int kbc_wait_read_mo(void)
{
	int retries = 10000;

	while (retries-- && !kbc_data_ready_mo())
		udelay(50);

	return (retries <= 0) ? -1 : kbc_data_get_mo();
}
