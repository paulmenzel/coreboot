/*
 * Copyright (C) 1991,1992,1993,1997,1998,2003, 2005 Free Software Foundation, Inc.
 * This file is part of the GNU C Library.
 * Copyright (c) 2011 The Chromium OS Authors.
 * Copyright (c) 2017 Patrick Rudolph <siro@das-labor.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/* From glibc-2.14, sysdeps/i386/memset.c
 * Modified to use SSE if available
 */

#include "cbui.h"
#include "cpuid.h"

static void *__memcpy_unaligned(void *dest, const void *src, size_t n)
{
	unsigned long d0, d1, d2;

	asm volatile(
		"rep ; movsl\n\t"
		"movl %4,%%ecx\n\t"
		"rep ; movsb\n\t"
		: "=&c" (d0), "=&D" (d1), "=&S" (d2)
		: "0" (n >> 2), "g" (n & 3), "1" (dest), "2" (src)
		: "memory"
	);

	return dest;
}

#ifdef __SSE__
static void *__memcpy_sse2_unaligned(u8 *to, const u8 *from, size_t n)
{
	size_t i;

	for (i = n >> 6; i > 0; i--) {
		__asm__ __volatile__ (
		"movups 0x00(%0), %%xmm0\n"
		"movups 0x10(%0), %%xmm1\n"
		"movups 0x20(%0), %%xmm2\n"
		"movups 0x30(%0), %%xmm3\n"
		"movntps %%xmm0, 0x00(%1)\n"
		"movntps %%xmm1, 0x10(%1)\n"
		"movntps %%xmm2, 0x20(%1)\n"
		"movntps %%xmm3, 0x30(%1)\n"
		:: "r" (from), "r" (to)
		: "memory", "xmm0", "xmm1", "xmm2", "xmm3");
		from += 64;
		to += 64;
	}

	return to;
}

static void *__memcpy_sse2(u8 *to, const u8 *from, size_t n)
{
	size_t i;

	for (i = n >> 6; i > 0; i--) {
		__asm__ __volatile__ (
		"movaps 0x00(%0), %%xmm0\n"
		"movaps 0x10(%0), %%xmm1\n"
		"movaps 0x20(%0), %%xmm2\n"
		"movaps 0x30(%0), %%xmm3\n"
		"movntps %%xmm0, 0x00(%1)\n"
		"movntps %%xmm1, 0x10(%1)\n"
		"movntps %%xmm2, 0x20(%1)\n"
		"movntps %%xmm3, 0x30(%1)\n"
		:: "r" (from), "r" (to)
		: "memory", "xmm0", "xmm1", "xmm2", "xmm3");
		from += 64;
		to += 64;
	}

	return to;
}

static void *__memcpy_sse4(u8 *to, const u8 *from, size_t n)
{
	size_t i;

	for (i = n >> 6; i > 0; i--) {
		__asm__ __volatile__ (
		"movntdqa 0x00(%0), %%xmm0\n"
		"movntdqa 0x10(%0), %%xmm1\n"
		"movntdqa 0x20(%0), %%xmm2\n"
		"movntdqa 0x30(%0), %%xmm3\n"
		"movntps %%xmm0, 0x00(%1)\n"
		"movntps %%xmm1, 0x10(%1)\n"
		"movntps %%xmm2, 0x20(%1)\n"
		"movntps %%xmm3, 0x30(%1)\n"
		:: "r" (from), "r" (to)
		: "memory", "xmm0", "xmm1", "xmm2", "xmm3");
		from += 64;
		to += 64;
	}

	return to;
}
#endif

void *memcpy_sse(void *dest, const void *src, size_t len)
{
	const u8 *from = (const u8 *)src;
	u8 *to = (u8 *)dest;

	/* Tests showed that REP is faster for small memory transfers.
	 * Use SSE to minimize cache pollution on large transfers. */
#ifdef __SSE__
	if (cpu_id.fid.bits.sse2 && len >= (1024 * 256)) {
		size_t delta;

		delta = ((uintptr_t)to) & 0xf;
		if (delta) {
			delta = 0x10 - delta;
			len -= delta;
			__memcpy_unaligned(to, from, delta);
			to += delta;
			from += delta;
		}
		if (((uintptr_t)from) & 0xf)
			__memcpy_sse2_unaligned(to, from, len);
		else if (cpu_id.fid.bits.sse41)
			__memcpy_sse4(to, from, len);
		else
			__memcpy_sse2(to, from, len);

		__asm__ __volatile__ ("sfence":::"memory");

		to += (len >> 6) * 64;
		from += (len >> 6) * 64;
		len &= 63;
	}

	if (len)
#endif
		__memcpy_unaligned(to, from, len);

	return dest;
}
