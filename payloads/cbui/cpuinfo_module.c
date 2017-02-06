/*
 * This file is part of the coreinfo project.
 *
 * It is derived from the x86info project, which is GPLv2-licensed.
 *
 * Copyright (C) 2001-2007 Dave Jones <davej@codemonkey.org.uk>
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

#if IS_ENABLED(CONFIG_MODULE_CPUINFO)
#include <arch/rdtsc.h>
#include "cpuid.h"

#define VENDOR_INTEL 0x756e6547
#define VENDOR_AMD   0x68747541
#define VENDOR_CYRIX 0x69727943
#define VENDOR_IDT   0x746e6543
#define VENDOR_GEODE 0x646f6547
#define VENDOR_RISE  0x52697365
#define VENDOR_RISE2 0x65736952
#define VENDOR_SIS   0x20536953

/* CPUID 0x00000001 Intel EDX flags */
static const char *intel_cap_edx_flags[] = {
	"fpu", "vme", "de", "pse", "tsc", "msr", "pae", "mce",
	"cx8", "apic", NULL, "sep", "mtrr", "pge", "mca", "cmov",
	"pat", "pse36", "psn", "clflsh", NULL, "ds", "acpi", "mmx",
	"fxsr", "sse", "sse2", "ss", "ht", "tm", "ia64", "pbe"
};

/* CPUID 0x00000001 AMD EDX flags */
static const char *amd_cap_edx_flags[] = {
	"fpu", "vme", "de", "pse", "tsc", "msr", "pae", "mce",
	"cx8", "apic", NULL, "sep", "mtrr", "pge", "mca", "cmov",
	"pat", "pse36", "psn", "clflsh", NULL, NULL, NULL, "mmx",
	"fxsr", "sse", "sse2", NULL, "ht", "tm", "ia64", "pbe"
};

/* CPUID 0x00000001 Intel ECX flags */
static const char *intel_cap_ecx_flags[] = {
	"sse3", "mulq", "dtes64", "monitor", "ds-cpl", "vmx", "smx", "est",
	"tm2", "ssse3", "cntx-id", NULL, NULL, "cmpxchg16b", "xTPR", "pdcm",
	NULL, NULL, "dca", "sse41", "sse42", "x2apic", "movbe", "popcnt",
	NULL, "aesni", "xsave", "osxsave", "avx", "f16c", "rdrand", "hypervisor"
};

/* CPUID 0x00000001 AMD ECX flags */
static const char *amd_cap_ecx_flags[] = {
	"sse3", "mulq", NULL, "monitor", NULL, NULL, NULL, NULL,
	NULL, "ssse3", NULL, NULL, "fma", "cmpxchg16b", NULL, NULL,
	NULL, NULL, NULL, "sse41", "sse42", NULL, NULL, "popcnt",
	NULL, "aesni", "xsave", "osxsave", "avx", "f16c", NULL, "hypervisor"
};

/* CPUID 0x80000001 Intel EDX flags */
static const char *intel_cap_extended_edx_flags[] = {
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, "SYSCALL", NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, "xd", NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, "em64t", NULL, NULL,
};

/* CPUID 0x80000001 Intel ECX flags */
static const char *intel_cap_extended_ecx_flags[] = {
	"lahf_lm", NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
};

/* CPUID 0x80000001 AMD EDX flags */
static const char *amd_cap_extended_edx_flags[] = {
	"fpu", "vme", "de", "pse", "tsc", "msr", "pae", "mce",
	"cx8", "apic", NULL, "sep", "mtrr", "pge", "mca", "cmov",
	"pat", "pse36", NULL, "mp", "nx", NULL, "mmxext", "mmx",
	"fxsr", "ffxsr", "page1gb", "rdtscp",
	NULL, "lm", "3dnowext", "3dnow"
}; /* "mp" defined for CPUs prior to AMD family 0xf */

/* CPUID 0x80000001 AMD ECX flags */
static const char *amd_cap_extended_ecx_flags[] = {
	"lahf/sahf", "CmpLegacy", "svm", "ExtApicSpace",
	"LockMovCr0", "abm", "sse4a", "misalignsse",
	"3dnowPref", "osvw", "ibs", NULL, "skinit", "wdt", NULL, "lwp",
	"fma4", NULL, NULL, "nodeid", NULL, "tbm", NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static char cpu_vendor[32];
static char cpu_name[64];
static char cpu_family[32];
static char cpu_model[32];
static char cpu_stepping[32];
static char cpu_khz[32];
static const char *cpu_features[64];
static const char *cpu_ext_features[64];
static int num_features, num_ext_features;

static int decode_flags(const char **features, unsigned long reg, const char **flags)
{
	int i, j;

	j = 0;
	for (i = 0; i < 32; i++) {
		if (flags[i] == NULL)
			continue;
		if (reg & (1 << i))
			features[j++] = flags[i];
	}

	return j;
}

static int cpuinfo_module_redraw(struct nk_context *ctx)
{
	int i;
	float columns;
	nk_flags flags =
	    NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_TITLE;

	nk_layout_row_dynamic(ctx, get_textline_height(), 1);
	nk_label(ctx, "CPU Information", NK_TEXT_ALIGN_LEFT);

	/* Make sure there's enough space in each groupbox */
	columns = 1 + MAX(5, MAX(num_features >> 2, num_ext_features >> 2));
	nk_layout_row_dynamic(ctx, get_textpad_height() * columns, 3);

	if (nk_group_begin(ctx, "General", flags)) {
		nk_layout_row_dynamic(ctx, get_textline_height(), 1);
		nk_label(ctx, cpu_vendor, NK_TEXT_ALIGN_LEFT);
		nk_label(ctx, cpu_name, NK_TEXT_ALIGN_LEFT);
		nk_label(ctx, cpu_family, NK_TEXT_ALIGN_LEFT);
		nk_label(ctx, cpu_model, NK_TEXT_ALIGN_LEFT);
		nk_label(ctx, cpu_khz, NK_TEXT_ALIGN_LEFT);
		nk_group_end(ctx);
	}

	if (nk_group_begin(ctx, "Features", flags)) {
		nk_layout_row_dynamic(ctx, get_textline_height(), 4);
		for (i = 0; i < num_features; i++)
			if (cpu_features[i])
				nk_label(ctx, cpu_features[i], NK_TEXT_ALIGN_LEFT);
		nk_group_end(ctx);
	}

	if (nk_group_begin(ctx, "Extended Features", flags)) {
		nk_layout_row_dynamic(ctx, get_textline_height(), 4);
		for (i = 0; i < num_ext_features; i++)
			if (cpu_ext_features[i])
				nk_label(ctx, cpu_ext_features[i], NK_TEXT_ALIGN_LEFT);
		nk_group_end(ctx);
	}
	/* FIXME
	if (vendor == VENDOR_AMD) {
		docpuid(0x80000001, &eax, &ebx, &ecx, &edx);
		brand = ((ebx >> 9) & 0x1f);

		mvwprintw(win, row++, 1, "Brand: %X", brand);
	}*/

	return 0;
}

static int cpuinfo_module_init(void)
{
	char *vendor;
	const char **features;
	int n;

	/* Get vendor */
	switch (cpu_id.vend_id.uint32_array[0]) {
	case VENDOR_INTEL:
		vendor = "Intel";
		break;
	case VENDOR_AMD:
		vendor = "AMD";
		break;
	case VENDOR_CYRIX:
		vendor = "Cyrix";
		break;
	case VENDOR_IDT:
		vendor = "IDT";
		break;
	case VENDOR_GEODE:
		vendor = "NatSemi Geode";
		break;
	case VENDOR_RISE:
	case VENDOR_RISE2:
		vendor = "RISE";
		break;
	case VENDOR_SIS:
		vendor = "SiS";
		break;
	default:
		vendor = "Unknown";
		break;
	}
	sprintf(cpu_vendor, "Vendor: %s", vendor);

	sprintf(cpu_name, "Processor: %s", cpu_id.brand_id.char_array);

	if (cpu_id.vers.bits.family == 0xf)
		sprintf(cpu_family, "Family: 0x%X",
		    cpu_id.vers.bits.family + cpu_id.vers.bits.extendedFamily);
	else
		sprintf(cpu_family, "Family: 0x%X",
		    cpu_id.vers.bits.family);

	if (cpu_id.vers.bits.family == 0x6 || cpu_id.vers.bits.family == 0xf)
		sprintf(cpu_model, "Model: 0x%X",
		    cpu_id.vers.bits.model + cpu_id.vers.bits.extendedModel * 16);
	else
		sprintf(cpu_model, "Model: 0x%X",
		    cpu_id.vers.bits.model);

	sprintf(cpu_stepping, "Stepping: 0x%X",
	    cpu_id.vers.bits.stepping);

	sprintf(cpu_khz, "CPU Speed: %d Mhz", lib_sysinfo.cpu_khz / 1000);

	num_features = 0;
	num_ext_features = 0;
	/* Extended flags */
	switch (cpu_id.vend_id.uint32_array[0]) {
	case VENDOR_AMD:
		features = cpu_features;
		n = decode_flags(features, cpu_id.fid.uint32_array[0], amd_cap_edx_flags);
		num_features += n;
		features += n;
		n = decode_flags(features, cpu_id.fid.uint32_array[1], amd_cap_ecx_flags);
		num_features += n;

		features = cpu_ext_features;
		n = decode_flags(features, cpu_id.fid.uint32_array[2], amd_cap_extended_edx_flags);
		num_ext_features += n;
		features += n;
		n = decode_flags(features, cpu_id.fid.uint32_array[3], amd_cap_extended_ecx_flags);
		num_ext_features += n;
		break;
	default:
	case VENDOR_INTEL:
		features = cpu_features;
		n = decode_flags(features, cpu_id.fid.uint32_array[0], intel_cap_edx_flags);
		num_features += n;
		features += n;
		n = decode_flags(features, cpu_id.fid.uint32_array[1], intel_cap_ecx_flags);
		num_features += n;

		features = cpu_ext_features;
		n = decode_flags(features, cpu_id.fid.uint32_array[2], intel_cap_extended_edx_flags);
		num_ext_features += n;
		features += n;
		n = decode_flags(features, cpu_id.fid.uint32_array[3], intel_cap_extended_ecx_flags);
		num_ext_features += n;
		break;
	}

	return 0;
}

static int cpuinfo_module_handle(int key)
{
	return 0;
}

struct cbui_module cpuinfo_module = {
	.name = "CPU Info",
	.init = cpuinfo_module_init,
	.redraw = cpuinfo_module_redraw,
	.handle = cpuinfo_module_handle,
};

#else

struct cbui_module cpuinfo_module = {
};

#endif
