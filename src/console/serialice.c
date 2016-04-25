/*
 * SerialICE
 *
 * Copyright (C) 2009 coresystems GmbH
 * Copyright (C) 2016 Patrick Rudolph <siro@das-labor.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <console/console.h>
#include <console/usb.h>
#include <console/uart.h>
#include <console/serialice.h>
#include <arch/io.h>
#include "serialice_priv.h"

#define VERSION "1.6"

/* Uart wrapper functions */

static void sio_flush(void)
{
	__usb_tx_flush();
	__uart_tx_flush();
}

static void sio_putc(u8 byte)
{
	__usb_tx_byte(byte);
	__uart_tx_byte(byte);
}

static u8 sio_getc(void)
{
	u8 val = 0;

	val = __usb_rx_byte();
	if (val) {
#ifdef ECHO_MODE
		sio_putc(val);
#endif
		return val;
	}

	val = __uart_rx_byte();
	if (val) {
#ifdef ECHO_MODE
		sio_putc(val);
#endif
		return val;
	}
	return 0;
}

/* String functions */

static void sio_putstring(const char *string)
{
	/* Very simple, no %d, %x etc. */
	while (*string) {
		if (*string == '\n')
			sio_putc('\r');
		sio_putc(*string);
		string++;
	}
	sio_flush();
}

static void sio_put8(u8 data)
{
	u8 c;

	c = (data >> 4) & 0xf;
	sio_put_nibble(c);

	c = data & 0xf;
	sio_put_nibble(c);
	sio_flush();
}

static void sio_put16(u16 data)
{
	int i;
	u8 c;

	for (i = 12; i >= 0; i -= 4) {
		c = (data >> i) & 0xf;
		sio_put_nibble(c);
	}
	sio_flush();
}

static void sio_put32(u32 data)
{
	int i;
	u8 c;

	for (i = 28; i >= 0; i -= 4) {
		c = (data >> i) & 0xf;
		sio_put_nibble(c);
	}
	sio_flush();
}

static u8 sio_get_nibble(void)
{
	u8 ret = 0;
	u8 nibble = sio_getc();

	if (nibble >= '0' && nibble <= '9') {
		ret = (nibble - '0');
	} else if (nibble >= 'a' && nibble <= 'f') {
		ret = (nibble - 'a') + 0xa;
	} else if (nibble >= 'A' && nibble <= 'F') {
		ret = (nibble - 'A') + 0xa;
	} else {
		sio_putstring("ERROR: parsing number\n");
	}
	return ret;
}

static u8 sio_get8(void)
{
	u8 data;

	data = sio_get_nibble();
	data = data << 4;
	data |= sio_get_nibble();
	return data;
}

static u16 sio_get16(void)
{
	u16 data;

	data = sio_get_nibble();
	data = data << 4;
	data |= sio_get_nibble();
	data = data << 4;
	data |= sio_get_nibble();
	data = data << 4;
	data |= sio_get_nibble();

	return data;
}

static u32 sio_get32(void)
{
	u32 data;

	data = sio_get_nibble();
	data = data << 4;
	data |= sio_get_nibble();
	data = data << 4;
	data |= sio_get_nibble();
	data = data << 4;
	data |= sio_get_nibble();
	data = data << 4;
	data |= sio_get_nibble();
	data = data << 4;
	data |= sio_get_nibble();
	data = data << 4;
	data |= sio_get_nibble();
	data = data << 4;
	data |= sio_get_nibble();

	return data;
}

/* SerialICE interface functions */

static void serialice_read_memory(void)
{
	u8 width;
	u32 *addr;

	// Format:
	// *rm00000000.w
	addr = (u32 *)sio_get32();
	sio_getc();	// skip .
	width = sio_getc();

	sio_putc('\r'); sio_putc('\n');

	switch (width) {
	case 'b':
		sio_put8(read8(addr));
		break;
	case 'w':
		sio_put16(read16(addr));
		break;
	case 'l':
		sio_put32(read32(addr));
		break;
	}
}

static void serialice_write_memory(void)
{
	u8 width;
	u32 *addr;
	u32 data;

	// Format:
	// *wm00000000.w=0000
	addr = (u32 *)sio_get32();
	sio_getc();	// skip .
	width = sio_getc();
	sio_getc();	// skip =

	switch (width) {
	case 'b':
		data = sio_get8();
		write8(addr, (u8)data);
		break;
	case 'w':
		data = sio_get16();
		write16(addr, (u16)data);
		break;
	case 'l':
		data = sio_get32();
		write32(addr, (u32)data);
		break;
	}
}

static void serialice_read_io(void)
{
	u8 width;
	u16 port;

	// Format:
	// *ri0000.w
	port = sio_get16();
	sio_getc();	// skip .
	width = sio_getc();

	sio_putc('\r'); sio_putc('\n');

	switch (width) {
	case 'b':
		sio_put8(inb(port));
		break;
	case 'w':
		sio_put16(inw(port));
		break;
	case 'l':
		sio_put32(inl(port));
		break;
	}
}

static void serialice_write_io(void)
{
	u8 width;
	u16 port;
	u32 data;

	// Format:
	// *wi0000.w=0000
	port = sio_get16();
	sio_getc();	// skip .
	width = sio_getc();
	sio_getc();	// skip =

	switch (width) {
	case 'b':
		data = sio_get8();
		outb((u8)data, port);
		break;
	case 'w':
		data = sio_get16();
		outw((u16)data, port);
		break;
	case 'l':
		data = sio_get32();
		outl((u32)data, port);
		break;
	}
}

static void serialice_read_msr(void)
{
	u32 addr, key;
	msr_t msr;

	// Format:
	// *rc00000000.9c5a203a
	addr = sio_get32();
	sio_getc();	   // skip .
	key = sio_get32(); // key in %edi

	sio_putc('\r'); sio_putc('\n');

	msr = rdmsr(addr, key);
	sio_put32(msr.hi);
	sio_putc('.');
	sio_put32(msr.lo);
}

static void serialice_write_msr(void)
{
	u32 addr, key;
	msr_t msr;

	// Format:
	// *wc00000000.9c5a203a=00000000.00000000
	addr = sio_get32();
	sio_getc();	// skip .
	key = sio_get32(); // read key in %edi
	sio_getc();	// skip =
	msr.hi = sio_get32();
	sio_getc();	// skip .
	msr.lo = sio_get32();

#ifdef __ROMCC__
	/* Cheat to avoid register outage */
	wrmsr(addr, msr, 0x9c5a203a);
#else
	wrmsr(addr, msr, key);
#endif
}

static void serialice_cpuinfo(void)
{
	u32 eax, ecx;
	u32 reg32;

	// Format:
	//    --EAX--- --ECX---
	// *ci00000000.00000000
	eax = sio_get32();
	sio_getc(); // skip .
	ecx = sio_get32();

	sio_putc('\r'); sio_putc('\n');

	/* This code looks quite crappy but this way we don't
	 * have to worry about running out of registers if we
	 * occupy eax, ebx, ecx, edx at the same time
	 */
	reg32 = cpuid_eax(eax, ecx);
	sio_put32(reg32);
	sio_putc('.');

	reg32 = cpuid_ebx(eax, ecx);
	sio_put32(reg32);
	sio_putc('.');

	reg32 = cpuid_ecx(eax, ecx);
	sio_put32(reg32);
	sio_putc('.');

	reg32 = cpuid_edx(eax, ecx);
	sio_put32(reg32);
}

static void serialice_mainboard(void)
{
	int i = 0;
	const char mb_string[] = CONFIG_MAINBOARD_VENDOR" "
			CONFIG_MAINBOARD_PART_NUMBER;

	sio_putc('\r'); sio_putc('\n');

	while (i < 32 && mb_string[i] > 0) {
		sio_putc(mb_string[i]);
		i++;
	}
	while (i < 32) {
		sio_putc(' ');
		i++;
	}

	sio_flush();
}

static void serialice_version(void)
{
	sio_putstring("\nSerialICE v" VERSION "\n");
}

void serialice_main(void)
{
	u16 c;

	serialice_version();

	while (1) {
		sio_putstring("\n> ");

		c = sio_getc();
		if (c != '*')
			continue;

		c = sio_getc() << 8;
		c |= sio_getc();

		switch (c) {
		case (('r' << 8)|'m'): // Read Memory *rm
			serialice_read_memory();
			break;
		case (('w' << 8)|'m'): // Write Memory *wm
			serialice_write_memory();
			break;
		case (('r' << 8)|'i'): // Read IO *ri
			serialice_read_io();
			break;
		case (('w' << 8)|'i'): // Write IO *wi
			serialice_write_io();
			break;
		case (('r' << 8)|'c'): // Read CPU MSR *rc
			serialice_read_msr();
			break;
		case (('w' << 8)|'c'): // Write CPU MSR *wc
			serialice_write_msr();
			break;
		case (('c' << 8)|'i'): // Read CPUID *ci
			serialice_cpuinfo();
			break;
		case (('m' << 8)|'b'): // Read mainboard type *mb
			serialice_mainboard();
			break;
		case (('v' << 8)|'i'): // Read version info *vi
			serialice_version();
			break;
		default:
			sio_putstring("ERROR\n");
			break;
		}
	}
}
