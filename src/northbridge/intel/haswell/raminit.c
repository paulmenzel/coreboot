/*
 * This file is part of the coreboot project.
 *
 * Copyright (C) 2011 Google Inc.
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

#include <console/console.h>
#include <console/usb.h>

#include <bootmode.h>
#include <string.h>
#include <arch/io.h>
#include <arch/cbfs.h>
#include <arch/cpu.h>
#include <cbmem.h>
#include <cbfs.h>
#include <halt.h>
#include <ip_checksum.h>
#include <northbridge/intel/common/mrc_cache.h>
#include <pc80/mc146818rtc.h>
#include <device/pci_def.h>
#include <device/device.h>
#include <device/pci_def.h>
#include <device/dram/ddr3.h>

#include "raminit.h"
#include "pei_data.h"
#include "haswell.h"

#include <timestamp.h>
#include <pc80/mc146818rtc.h>
#include <memory_info.h>
#include <smbios.h>
#include <delay.h>
#include <lib.h>

/* Management Engine is in the southbridge */
#include "southbridge/intel/lynxpoint/me.h"
/* For SPD.  */
#include "cpu/x86/msr.h"

/* FIXME: no ECC support.  */
/* FIXME: no support for 3-channel chipsets.  */

/*
 * Register description:
 * Intel provides a command queue of depth four.
 * Every command is configured by using multiple registers.
 * On executing the command queue you have to provide the depth used.
 *
 * Known registers:
 * Channel X = [0, 1]
 * Command queue index Y = [0, 1, 2, 3]
 *
 * DEFAULT_MCHBAR + 0x4220 + 0x400 * X + 4 * Y: command io register
 *  Controls the DRAM command signals
 *  Bit 0: !RAS
 *  Bit 1: !CAS
 *  Bit 2: !WE
 *
 * DEFAULT_MCHBAR + 0x4200 + 0x400 * X + 4 * Y: addr bankslot io register
 *  Controls the address, bank address and slotrank signals
 *  Bit 0-15 : Address
 *  Bit 20-22: Bank Address
 *  Bit 24-25: slotrank
 *
 * DEFAULT_MCHBAR + 0x4230 + 0x400 * X + 4 * Y: idle register
 *  Controls the idle time after issuing this DRAM command
 *  Bit 16-32: number of clock-cylces to idle
 *
 * DEFAULT_MCHBAR + 0x4284 + 0x400 * channel: execute command queue
 *  Starts to execute all queued commands
 *  Bit 0    : start DRAM command execution
 *  Bit 16-20: (number of queued commands - 1) * 4
 */

#define BASEFREQ 133
#define tDLLK 512

#define IS_SANDY_CPU(x) ((x & 0xffff0) == 0x206a0)
#define IS_SANDY_CPU_C(x) ((x & 0xf) == 4)
#define IS_SANDY_CPU_D0(x) ((x & 0xf) == 5)
#define IS_SANDY_CPU_D1(x) ((x & 0xf) == 6)
#define IS_SANDY_CPU_D2(x) ((x & 0xf) == 7)

#define IS_IVY_CPU(x) ((x & 0xffff0) == 0x306a0)
#define IS_IVY_CPU_C(x) ((x & 0xf) == 4)
#define IS_IVY_CPU_K(x) ((x & 0xf) == 5)
#define IS_IVY_CPU_D(x) ((x & 0xf) == 6)
#define IS_IVY_CPU_E(x) ((x & 0xf) >= 8)

#define NUM_CHANNELS 2
#define NUM_SLOTRANKS 4
#define NUM_SLOTS 2
#define NUM_LANES 8

/* FIXME: Vendor BIOS uses 64 but our algorithms are less
   performant and even 1 seems to be enough in practice.  */
#define NUM_PATTERNS 4

typedef struct odtmap_st {
	u16 rttwr;
	u16 rttnom;
} odtmap;

typedef struct dimm_info_st {
	dimm_attr dimm[NUM_CHANNELS][NUM_SLOTS];
} dimm_info;

struct ram_rank_timings {
	/* Register 4024. One byte per slotrank.  */
	u8 val_4024;
	/* Register 4028. One nibble per slotrank.  */
	u8 val_4028;

	int val_320c;

	struct ram_lane_timings {
		/* lane register offset 0x10.  */
		u16 timA;	/* bits 0 - 5, bits 16 - 18 */
		u8 rising;	/* bits 8 - 14 */
		u8 falling;	/* bits 20 - 26.  */

		/* lane register offset 0x20.  */
		int timC;	/* bit 0 - 5, 19.  */
		u16 timB;	/* bits 8 - 13, 15 - 17.  */
	} lanes[NUM_LANES];
};

struct ramctr_timing_st;

typedef struct ramctr_timing_st {
	u16 spd_crc[NUM_CHANNELS][NUM_SLOTS];
	int mobile;

	u16 cas_supported;
	/* tLatencies are in units of ns, scaled by x256 */
	u32 tCK;
	u32 tAA;
	u32 tWR;
	u32 tRCD;
	u32 tRRD;
	u32 tRP;
	u32 tRAS;
	u32 tRFC;
	u32 tWTR;
	u32 tRTP;
	u32 tFAW;
	/* Latencies in terms of clock cycles
	 * They are saved separately as they are needed for DRAM MRS commands*/
	u8 CAS;			/* CAS read latency */
	u8 CWL;			/* CAS write latency */

	u32 tREFI;
	u32 tMOD;
	u32 tXSOffset;
	u32 tWLO;
	u32 tCKE;
	u32 tXPDLL;
	u32 tXP;
	u32 tAONPD;

	u16 reg_5064b0; /* bits 0-11. */

	u8 rankmap[NUM_CHANNELS];
	int ref_card_offset[NUM_CHANNELS];
	u32 mad_dimm[NUM_CHANNELS];
	int channel_size_mb[NUM_CHANNELS];
	u32 cmd_stretch[NUM_CHANNELS];

	int reg_c14_offset;
	int reg_320c_range_threshold;

	int edge_offset[3];
	int timC_offset[3];

	int extended_temperature_range;
	int auto_self_refresh;

	int rank_mirror[NUM_CHANNELS][NUM_SLOTRANKS];

	struct ram_rank_timings timings[NUM_CHANNELS][NUM_SLOTRANKS];

	dimm_info info;
} ramctr_timing;

#define SOUTHBRIDGE PCI_DEV(0, 0x1f, 0)
#define NORTHBRIDGE PCI_DEV(0, 0x0, 0)
#define FOR_ALL_LANES for (lane = 0; lane < NUM_LANES; lane++)
#define FOR_ALL_CHANNELS for (channel = 0; channel < NUM_CHANNELS; channel++)
#define FOR_ALL_POPULATED_RANKS for (slotrank = 0; slotrank < NUM_SLOTRANKS; slotrank++) if (ctrl->rankmap[channel] & (1 << slotrank))
#define FOR_ALL_POPULATED_CHANNELS for (channel = 0; channel < NUM_CHANNELS; channel++) if (ctrl->rankmap[channel])
#define MAX_EDGE_TIMING 71
#define MAX_TIMC 127
#define MAX_TIMB 511
#define MAX_TIMA 127

#define MAKE_ERR ((channel<<16)|(slotrank<<8)|1)

static const char *ecc_decoder[] = {
	"inactive",
	"active on IO",
	"disabled on IO",
	"active"
};

/*
 * Dump in the log memory controller configuration as read from the memory
 * controller registers.
 */
static void report_memory_config(void)
{
	u32 addr_decoder_common, addr_decode_ch[2];
	int i;

	addr_decoder_common = MCHBAR32(0x5000);
	addr_decode_ch[0] = MCHBAR32(0x5004);
	addr_decode_ch[1] = MCHBAR32(0x5008);

	printk(BIOS_DEBUG, "memcfg DDR3 clock %d MHz\n",
	       (MCHBAR32(0x5e04) * 13333 * 2 + 50)/100);
	printk(BIOS_DEBUG, "memcfg channel assignment: A: %d, B % d, C % d\n",
	       addr_decoder_common & 3,
	       (addr_decoder_common >> 2) & 3,
	       (addr_decoder_common >> 4) & 3);

	for (i = 0; i < ARRAY_SIZE(addr_decode_ch); i++) {
		u32 ch_conf = addr_decode_ch[i];
		printk(BIOS_DEBUG, "memcfg channel[%d] config (%8.8x):\n",
		       i, ch_conf);
		printk(BIOS_DEBUG, "   ECC %s\n",
		       ecc_decoder[(ch_conf >> 24) & 3]);
		printk(BIOS_DEBUG, "   enhanced interleave mode %s\n",
		       ((ch_conf >> 22) & 1) ? "on" : "off");
		printk(BIOS_DEBUG, "   rank interleave %s\n",
		       ((ch_conf >> 21) & 1) ? "on" : "off");
		printk(BIOS_DEBUG, "   DIMMA %d MB width %s %s rank%s\n",
		       ((ch_conf >> 0) & 0xff) * 256,
		       ((ch_conf >> 19) & 1) ? "x16" : "x8 or x32",
		       ((ch_conf >> 17) & 1) ? "dual" : "single",
		       ((ch_conf >> 16) & 1) ? "" : ", selected");
		printk(BIOS_DEBUG, "   DIMMB %d MB width %s %s rank%s\n",
		       ((ch_conf >> 8) & 0xff) * 256,
		       ((ch_conf >> 19) & 1) ? "x16" : "x8 or x32",
		       ((ch_conf >> 18) & 1) ? "dual" : "single",
		       ((ch_conf >> 16) & 1) ? ", selected" : "");
	}
}

#include <delay.h>
#include <stdint.h>
#include <cpu/x86/tsc.h>
#include <cpu/x86/msr.h>

static void wait_txt_clear(void)
{
	struct cpuid_result cp;

	cp = cpuid_ext(0x1, 0x0);
	/* Check if TXT is supported?  */
	if (!(cp.ecx & 0x40))
		return;
	/* Some TXT public bit.  */
	if (!(read32((void *)0xfed30010) & 1))
		return;
	/* Wait for TXT clear.  */
	while (!(read8((void *)0xfed40000) & (1 << 7))) ;
}
#if 0
static void sfence(void)
{
	asm volatile ("sfence");
}

static void toggle_io_reset(void) {
	/* toggle IO reset bit */
	u32 r32 = read32(DEFAULT_MCHBAR + 0x5030);
	write32(DEFAULT_MCHBAR + 0x5030, r32 | 0x20);
	udelay(1);
	write32(DEFAULT_MCHBAR + 0x5030, r32 & ~0x20);
	udelay(1);
}
#endif
/*
 * Fill cbmem with information for SMBIOS type 17.
 */
static void fill_smbios17(ramctr_timing *ctrl)
{
	struct memory_info *mem_info;
	int channel, slot;
	struct dimm_info *dimm;
	uint16_t ddr_freq;
	dimm_info *info = &ctrl->info;

	ddr_freq = (1000 << 8) / ctrl->tCK;

	/*
	 * Allocate CBMEM area for DIMM information used to populate SMBIOS
	 * table 17
	 */
	mem_info = cbmem_add(CBMEM_ID_MEMINFO, sizeof(*mem_info));
	printk(BIOS_DEBUG, "CBMEM entry for DIMM info: 0x%p\n", mem_info);
	if (!mem_info)
		return;

	memset(mem_info, 0, sizeof(*mem_info));

	FOR_ALL_CHANNELS for(slot = 0; slot < NUM_SLOTS; slot++) {
		dimm = &mem_info->dimm[mem_info->dimm_cnt];
		if (info->dimm[channel][slot].size_mb) {
			dimm->ddr_type = MEMORY_TYPE_DDR3;
			dimm->ddr_frequency = ddr_freq;
			dimm->dimm_size = info->dimm[channel][slot].size_mb;
			dimm->channel_num = channel;
			dimm->rank_per_dimm = info->dimm[channel][slot].ranks;
			dimm->dimm_num = slot;
			memcpy(dimm->module_part_number,
				   info->dimm[channel][slot].part_number, 16);
			dimm->mod_id = info->dimm[channel][slot].manufacturer_id;
			dimm->mod_type = info->dimm[channel][slot].dimm_type;
			dimm->bus_width = info->dimm[channel][slot].width;
			mem_info->dimm_cnt++;
		}
	}
}

/*
 * Return CRC16 match for all SPDs.
 */
static int verify_crc16_spds_ddr3(spd_raw_data *spd, ramctr_timing *ctrl)
{
	int channel, slot, spd_slot;
	int match = 1;

	FOR_ALL_CHANNELS {
		for (slot = 0; slot < NUM_SLOTS; slot++) {
			spd_slot = 2 * channel + slot;
			match &= ctrl->spd_crc[channel][slot] ==
					spd_ddr3_calc_crc(spd[spd_slot], sizeof(spd_raw_data));
		}
	}
	return match;
}

static void dram_find_spds_ddr3(spd_raw_data *spd, ramctr_timing *ctrl)
{
	int dimms = 0, dimms_on_channel;
	int channel, slot, spd_slot;
	dimm_info *dimm = &ctrl->info;

	memset (ctrl->rankmap, 0, sizeof (ctrl->rankmap));

	ctrl->extended_temperature_range = 1;
	ctrl->auto_self_refresh = 1;

	FOR_ALL_CHANNELS {
		ctrl->channel_size_mb[channel] = 0;

		dimms_on_channel = 0;
		/* count dimms on channel */
		for (slot = 0; slot < NUM_SLOTS; slot++) {
			spd_slot = 2 * channel + slot;
			spd_decode_ddr3(&dimm->dimm[channel][slot], spd[spd_slot]);
			if (dimm->dimm[channel][slot].dram_type == SPD_MEMORY_TYPE_SDRAM_DDR3)
				dimms_on_channel++;
		}

		for (slot = 0; slot < NUM_SLOTS; slot++) {
			spd_slot = 2 * channel + slot;
			/* search for XMP profile */
			spd_xmp_decode_ddr3(&dimm->dimm[channel][slot],
					spd[spd_slot],
					DDR3_XMP_PROFILE_1);

			if (dimm->dimm[channel][slot].dram_type != SPD_MEMORY_TYPE_SDRAM_DDR3) {
				printram("No valid XMP profile found.\n");
				spd_decode_ddr3(&dimm->dimm[channel][slot], spd[spd_slot]);
			} else if (dimms_on_channel > dimm->dimm[channel][slot].dimms_per_channel) {
				printram("XMP profile supports %u DIMMs, but %u DIMMs are installed.\n",
						 dimm->dimm[channel][slot].dimms_per_channel,
						 dimms_on_channel);
				spd_decode_ddr3(&dimm->dimm[channel][slot], spd[spd_slot]);
			} else if (dimm->dimm[channel][slot].voltage != 1500) {
				/* TODO: support other DDR3 voltage than 1500mV */
				printram("XMP profile's requested %u mV is unsupported.\n",
						 dimm->dimm[channel][slot].voltage);
				spd_decode_ddr3(&dimm->dimm[channel][slot], spd[spd_slot]);
			}

			/* fill in CRC16 for MRC cache */
			ctrl->spd_crc[channel][slot] =
					spd_ddr3_calc_crc(spd[spd_slot], sizeof(spd_raw_data));

			if (dimm->dimm[channel][slot].dram_type != SPD_MEMORY_TYPE_SDRAM_DDR3) {
				// set dimm invalid
				dimm->dimm[channel][slot].ranks = 0;
				dimm->dimm[channel][slot].size_mb = 0;
				continue;
			}

			dram_print_spd_ddr3(&dimm->dimm[channel][slot]);
			dimms++;
			ctrl->rank_mirror[channel][slot * 2] = 0;
			ctrl->rank_mirror[channel][slot * 2 + 1] = dimm->dimm[channel][slot].flags.pins_mirrored;
			ctrl->channel_size_mb[channel] += dimm->dimm[channel][slot].size_mb;

			ctrl->auto_self_refresh &= dimm->dimm[channel][slot].flags.asr;
			ctrl->extended_temperature_range &= dimm->dimm[channel][slot].flags.ext_temp_refresh;

			ctrl->rankmap[channel] |= ((1 << dimm->dimm[channel][slot].ranks) - 1) << (2 * slot);
			printk(BIOS_DEBUG, "channel[%d] rankmap = 0x%x\n",
			       channel, ctrl->rankmap[channel]);
		}
		if ((ctrl->rankmap[channel] & 3) && (ctrl->rankmap[channel] & 0xc)
			&& dimm->dimm[channel][0].reference_card <= 5 && dimm->dimm[channel][1].reference_card <= 5) {
			const int ref_card_offset_table[6][6] = {
				{ 0, 0, 0, 0, 2, 2, },
				{ 0, 0, 0, 0, 2, 2, },
				{ 0, 0, 0, 0, 2, 2, },
				{ 0, 0, 0, 0, 1, 1, },
				{ 2, 2, 2, 1, 0, 0, },
				{ 2, 2, 2, 1, 0, 0, },
			};
			ctrl->ref_card_offset[channel] = ref_card_offset_table[dimm->dimm[channel][0].reference_card]
				[dimm->dimm[channel][1].reference_card];
		} else
			ctrl->ref_card_offset[channel] = 0;
	}

	if (!dimms)
		die("No DIMMs were found");
}

static void dram_find_common_params(ramctr_timing *ctrl)
{
	size_t valid_dimms;
	int channel, slot;
	dimm_info *dimms = &ctrl->info;

	ctrl->cas_supported = 0xff;
	valid_dimms = 0;
	FOR_ALL_CHANNELS for (slot = 0; slot < 2; slot++) {
		const dimm_attr *dimm = &dimms->dimm[channel][slot];
		if (dimm->dram_type != SPD_MEMORY_TYPE_SDRAM_DDR3)
			continue;
		valid_dimms++;

		/* Find all possible CAS combinations */
		ctrl->cas_supported &= dimm->cas_supported;

		/* Find the smallest common latencies supported by all DIMMs */
		ctrl->tCK = MAX(ctrl->tCK, dimm->tCK);
		ctrl->tAA = MAX(ctrl->tAA, dimm->tAA);
		ctrl->tWR = MAX(ctrl->tWR, dimm->tWR);
		ctrl->tRCD = MAX(ctrl->tRCD, dimm->tRCD);
		ctrl->tRRD = MAX(ctrl->tRRD, dimm->tRRD);
		ctrl->tRP = MAX(ctrl->tRP, dimm->tRP);
		ctrl->tRAS = MAX(ctrl->tRAS, dimm->tRAS);
		ctrl->tRFC = MAX(ctrl->tRFC, dimm->tRFC);
		ctrl->tWTR = MAX(ctrl->tWTR, dimm->tWTR);
		ctrl->tRTP = MAX(ctrl->tRTP, dimm->tRTP);
		ctrl->tFAW = MAX(ctrl->tFAW, dimm->tFAW);
	}

	if (!ctrl->cas_supported)
		die("Unsupported DIMM combination. "
		    "DIMMS do not support common CAS latency");
	if (!valid_dimms)
		die("No valid DIMMs found");
}

static u8 get_CWL(u8 CAS)
{
	/* Get CWL based on CAS using the following rule:
	 *       _________________________________________
	 * CAS: | 4T | 5T | 6T | 7T | 8T | 9T | 10T | 11T |
	 * CWL: | 5T | 5T | 5T | 6T | 6T | 7T |  7T |  8T |
	 */
	static const u8 cas_cwl_map[] = { 5, 5, 5, 6, 6, 7, 7, 8 };
	if (CAS > 11)
		return 8;
	return cas_cwl_map[CAS - 4];
}

/* Frequency multiplier.  */
static u32 get_FRQ(u32 tCK)
{
	u32 FRQ;
	FRQ = 256000 / (tCK * BASEFREQ);
	if (FRQ > 8)
		return 8;
	if (FRQ < 3)
		return 3;
	return FRQ;
}

static u32 get_REFI(u32 tCK)
{
	/* Get REFI based on MCU frequency using the following rule:
	 *        _________________________________________
	 * FRQ : | 3    | 4    | 5    | 6    | 7    | 8    |
	 * REFI: | 3120 | 4160 | 5200 | 6240 | 7280 | 8320 |
	 */
	static const u32 frq_refi_map[] =
	    { 3120, 4160, 5200, 6240, 7280, 8320 };
	return frq_refi_map[get_FRQ(tCK) - 3];
}

static u8 get_XSOffset(u32 tCK)
{
	/* Get XSOffset based on MCU frequency using the following rule:
	 *             _________________________
	 * FRQ      : | 3 | 4 | 5 | 6 | 7  | 8  |
	 * XSOffset : | 4 | 6 | 7 | 8 | 10 | 11 |
	 */
	static const u8 frq_xs_map[] = { 4, 6, 7, 8, 10, 11 };
	return frq_xs_map[get_FRQ(tCK) - 3];
}

static u8 get_MOD(u32 tCK)
{
	/* Get MOD based on MCU frequency using the following rule:
	 *        _____________________________
	 * FRQ : | 3  | 4  | 5  | 6  | 7  | 8  |
	 * MOD : | 12 | 12 | 12 | 12 | 15 | 16 |
	 */
	static const u8 frq_mod_map[] = { 12, 12, 12, 12, 15, 16 };
	return frq_mod_map[get_FRQ(tCK) - 3];
}

static u8 get_WLO(u32 tCK)
{
	/* Get WLO based on MCU frequency using the following rule:
	 *        _______________________
	 * FRQ : | 3 | 4 | 5 | 6 | 7 | 8 |
	 * WLO : | 4 | 5 | 6 | 6 | 8 | 8 |
	 */
	static const u8 frq_wlo_map[] = { 4, 5, 6, 6, 8, 8 };
	return frq_wlo_map[get_FRQ(tCK) - 3];
}

static u8 get_CKE(u32 tCK)
{
	/* Get CKE based on MCU frequency using the following rule:
	 *        _______________________
	 * FRQ : | 3 | 4 | 5 | 6 | 7 | 8 |
	 * CKE : | 3 | 3 | 4 | 4 | 5 | 6 |
	 */
	static const u8 frq_cke_map[] = { 3, 3, 4, 4, 5, 6 };
	return frq_cke_map[get_FRQ(tCK) - 3];
}

static u8 get_XPDLL(u32 tCK)
{
	/* Get XPDLL based on MCU frequency using the following rule:
	 *          _____________________________
	 * FRQ   : | 3  | 4  | 5  | 6  | 7  | 8  |
	 * XPDLL : | 10 | 13 | 16 | 20 | 23 | 26 |
	 */
	static const u8 frq_xpdll_map[] = { 10, 13, 16, 20, 23, 26 };
	return frq_xpdll_map[get_FRQ(tCK) - 3];
}

static u8 get_XP(u32 tCK)
{
	/* Get XP based on MCU frequency using the following rule:
	 *        _______________________
	 * FRQ : | 3 | 4 | 5 | 6 | 7 | 8 |
	 * XP  : | 3 | 4 | 4 | 5 | 6 | 7 |
	 */
	static const u8 frq_xp_map[] = { 3, 4, 4, 5, 6, 7 };
	return frq_xp_map[get_FRQ(tCK) - 3];
}

static u8 get_AONPD(u32 tCK)
{
	/* Get AONPD based on MCU frequency using the following rule:
	 *          ________________________
	 * FRQ   : | 3 | 4 | 5 | 6 | 7 | 8  |
	 * AONPD : | 4 | 5 | 6 | 8 | 8 | 10 |
	 */
	static const u8 frq_aonpd_map[] = { 4, 5, 6, 8, 8, 10 };
	return frq_aonpd_map[get_FRQ(tCK) - 3];
}
#if 0
static u32 get_COMP2(u32 tCK)
{
	/* Get COMP2 based on MCU frequency using the following rule:
	 *         ___________________________________________________________
	 * FRQ  : | 3       | 4       | 5       | 6       | 7       | 8       |
	 * COMP : | D6BEDCC | CE7C34C | CA57A4C | C6369CC | C42514C | C21410C |
	 */
	static const u32 frq_comp2_map[] = { 0xD6BEDCC, 0xCE7C34C, 0xCA57A4C,
		0xC6369CC, 0xC42514C, 0xC21410C
	};
	return frq_comp2_map[get_FRQ(tCK) - 3];
}
#endif
static u32 get_XOVER_CLK(u8 rankmap)
{
	return rankmap << 24;
}

static u32 get_XOVER_CMD(u8 rankmap)
{
	u32 reg;

	// enable xover cmd
	reg = 0x4000;

	// enable xover ctl
	if (rankmap & 0x3)
		reg |= 0x20000;

	if (rankmap & 0xc)
		reg |= 0x4000000;

	return reg;
}

static void dram_timing(ramctr_timing * ctrl)
{
	u8 val;
	u32 val32;

	/* Maximum supported DDR3 frequency is 1066MHz (DDR3 2133) so make sure
	 * we cap it if we have faster DIMMs.
	 * Then, align it to the closest JEDEC standard frequency */
	if (ctrl->tCK <= TCK_1066MHZ) {
		ctrl->tCK = TCK_1066MHZ;
		ctrl->edge_offset[0] = 16;
		ctrl->edge_offset[1] = 7;
		ctrl->edge_offset[2] = 7;
		ctrl->timC_offset[0] = 18;
		ctrl->timC_offset[1] = 7;
		ctrl->timC_offset[2] = 7;
		ctrl->reg_c14_offset = 16;
		ctrl->reg_5064b0 = 0x218;
		ctrl->reg_320c_range_threshold = 13;
	} else if (ctrl->tCK <= TCK_933MHZ) {
		ctrl->tCK = TCK_933MHZ;
		ctrl->edge_offset[0] = 14;
		ctrl->edge_offset[1] = 6;
		ctrl->edge_offset[2] = 6;
		ctrl->timC_offset[0] = 15;
		ctrl->timC_offset[1] = 6;
		ctrl->timC_offset[2] = 6;
		ctrl->reg_c14_offset = 14;
		ctrl->reg_5064b0 = 0x1d5;
		ctrl->reg_320c_range_threshold = 15;
	} else if (ctrl->tCK <= TCK_800MHZ) {
		ctrl->tCK = TCK_800MHZ;
		ctrl->edge_offset[0] = 13;
		ctrl->edge_offset[1] = 5;
		ctrl->edge_offset[2] = 5;
		ctrl->timC_offset[0] = 14;
		ctrl->timC_offset[1] = 5;
		ctrl->timC_offset[2] = 5;
		ctrl->reg_c14_offset = 12;
		ctrl->reg_5064b0 = 0x193;
		ctrl->reg_320c_range_threshold = 15;
	} else if (ctrl->tCK <= TCK_666MHZ) {
		ctrl->tCK = TCK_666MHZ;
		ctrl->edge_offset[0] = 10;
		ctrl->edge_offset[1] = 4;
		ctrl->edge_offset[2] = 4;
		ctrl->timC_offset[0] = 11;
		ctrl->timC_offset[1] = 4;
		ctrl->timC_offset[2] = 4;
		ctrl->reg_c14_offset = 10;
		ctrl->reg_5064b0 = 0x150;
		ctrl->reg_320c_range_threshold = 16;
	} else if (ctrl->tCK <= TCK_533MHZ) {
		ctrl->tCK = TCK_533MHZ;
		ctrl->edge_offset[0] = 8;
		ctrl->edge_offset[1] = 3;
		ctrl->edge_offset[2] = 3;
		ctrl->timC_offset[0] = 9;
		ctrl->timC_offset[1] = 3;
		ctrl->timC_offset[2] = 3;
		ctrl->reg_c14_offset = 8;
		ctrl->reg_5064b0 = 0x10d;
		ctrl->reg_320c_range_threshold = 17;
	} else  {
		ctrl->tCK = TCK_400MHZ;
		ctrl->edge_offset[0] = 6;
		ctrl->edge_offset[1] = 2;
		ctrl->edge_offset[2] = 2;
		ctrl->timC_offset[0] = 6;
		ctrl->timC_offset[1] = 2;
		ctrl->timC_offset[2] = 2;
		ctrl->reg_c14_offset = 8;
		ctrl->reg_5064b0 = 0xcd;
		ctrl->reg_320c_range_threshold = 17;
	}

	val32 = (1000 << 8) / ctrl->tCK;
	printk(BIOS_DEBUG, "Selected DRAM frequency: %u MHz\n", val32);

	/* Find CAS and CWL latencies */
	val = (ctrl->tAA + ctrl->tCK - 1) / ctrl->tCK;
	printk(BIOS_DEBUG, "Minimum  CAS latency   : %uT\n", val);
	/* Find lowest supported CAS latency that satisfies the minimum value */
	while (!((ctrl->cas_supported >> (val - 4)) & 1)
	       && (ctrl->cas_supported >> (val - 4))) {
		val++;
	}
	/* Is CAS supported */
	if (!(ctrl->cas_supported & (1 << (val - 4))))
		printk(BIOS_DEBUG, "CAS not supported\n");
	printk(BIOS_DEBUG, "Selected CAS latency   : %uT\n", val);
	ctrl->CAS = val;
	ctrl->CWL = get_CWL(ctrl->CAS);
	printk(BIOS_DEBUG, "Selected CWL latency   : %uT\n", ctrl->CWL);

	/* Find tRCD */
	ctrl->tRCD = (ctrl->tRCD + ctrl->tCK - 1) / ctrl->tCK;
	printk(BIOS_DEBUG, "Selected tRCD          : %uT\n", ctrl->tRCD);

	ctrl->tRP = (ctrl->tRP + ctrl->tCK - 1) / ctrl->tCK;
	printk(BIOS_DEBUG, "Selected tRP           : %uT\n", ctrl->tRP);

	/* Find tRAS */
	ctrl->tRAS = (ctrl->tRAS + ctrl->tCK - 1) / ctrl->tCK;
	printk(BIOS_DEBUG, "Selected tRAS          : %uT\n", ctrl->tRAS);

	/* Find tWR */
	ctrl->tWR = (ctrl->tWR + ctrl->tCK - 1) / ctrl->tCK;
	printk(BIOS_DEBUG, "Selected tWR           : %uT\n", ctrl->tWR);

	/* Find tFAW */
	ctrl->tFAW = (ctrl->tFAW + ctrl->tCK - 1) / ctrl->tCK;
	printk(BIOS_DEBUG, "Selected tFAW          : %uT\n", ctrl->tFAW);

	/* Find tRRD */
	ctrl->tRRD = (ctrl->tRRD + ctrl->tCK - 1) / ctrl->tCK;
	printk(BIOS_DEBUG, "Selected tRRD          : %uT\n", ctrl->tRRD);

	/* Find tRTP */
	ctrl->tRTP = (ctrl->tRTP + ctrl->tCK - 1) / ctrl->tCK;
	printk(BIOS_DEBUG, "Selected tRTP          : %uT\n", ctrl->tRTP);

	/* Find tWTR */
	ctrl->tWTR = (ctrl->tWTR + ctrl->tCK - 1) / ctrl->tCK;
	printk(BIOS_DEBUG, "Selected tWTR          : %uT\n", ctrl->tWTR);

	/* Refresh-to-Active or Refresh-to-Refresh (tRFC) */
	ctrl->tRFC = (ctrl->tRFC + ctrl->tCK - 1) / ctrl->tCK;
	printk(BIOS_DEBUG, "Selected tRFC          : %uT\n", ctrl->tRFC);

	ctrl->tREFI = get_REFI(ctrl->tCK);
	ctrl->tMOD = get_MOD(ctrl->tCK);
	ctrl->tXSOffset = get_XSOffset(ctrl->tCK);
	ctrl->tWLO = get_WLO(ctrl->tCK);
	ctrl->tCKE = get_CKE(ctrl->tCK);
	ctrl->tXPDLL = get_XPDLL(ctrl->tCK);
	ctrl->tXP = get_XP(ctrl->tCK);
	ctrl->tAONPD = get_AONPD(ctrl->tCK);
}

static void dram_freq(ramctr_timing * ctrl)
{
	if (ctrl->tCK > TCK_400MHZ) {
		printk (BIOS_ERR, "DRAM frequency is under lowest supported frequency (400 MHz). Increasing to 400 MHz as last resort");
		ctrl->tCK = TCK_400MHZ;
	}
	while (1) {
		u8 val2;
		u32 reg1 = 0;

		/* Step 1 - Set target PCU frequency */

		if (ctrl->tCK <= TCK_1066MHZ) {
			ctrl->tCK = TCK_1066MHZ;
		} else if (ctrl->tCK <= TCK_933MHZ) {
			ctrl->tCK = TCK_933MHZ;
		} else if (ctrl->tCK <= TCK_800MHZ) {
			ctrl->tCK = TCK_800MHZ;
		} else if (ctrl->tCK <= TCK_666MHZ) {
			ctrl->tCK = TCK_666MHZ;
		} else if (ctrl->tCK <= TCK_533MHZ) {
			ctrl->tCK = TCK_533MHZ;
		} else if (ctrl->tCK <= TCK_400MHZ) {
			ctrl->tCK = TCK_400MHZ;
		} else {
			die ("No lock frequency found");
		}

		/* Frequency mulitplier.  */
		u32 FRQ = get_FRQ(ctrl->tCK);

		/* Step 2 - Select frequency in the MCU */
		reg1 = FRQ;
		reg1 |= 0x80000000;	// set running bit
		MCHBAR32(0x5e00) = reg1;
		while (reg1 & 0x80000000) {
			printk(BIOS_DEBUG, " PLL busy...");
			reg1 = MCHBAR32(0x5e00);
		}
		printk(BIOS_DEBUG, "done\n");

		/* Step 3 - Verify lock frequency */
		reg1 = MCHBAR32(0x5e04);
		val2 = (u8) reg1;
		if (val2 >= FRQ) {
			printk(BIOS_DEBUG, "MCU frequency is set at : %d MHz\n",
			       (1000 << 8) / ctrl->tCK);
			return;
		}
		printk(BIOS_DEBUG, "PLL didn't lock. Retrying at lower frequency\n");
		ctrl->tCK++;
	}
}

static void dram_xover(ramctr_timing * ctrl)
{
	u32 reg;
	int channel;

	FOR_ALL_CHANNELS {
		// enable xover clk
		reg = get_XOVER_CLK(ctrl->rankmap[channel]);
		printram("XOVER CLK [%x] = %x\n", channel * 0x100 + 0xc14,
			 reg);
		MCHBAR32(channel * 0x100 + 0xc14) = reg;

		// enable xover ctl & xover cmd
		reg = get_XOVER_CMD(ctrl->rankmap[channel]);
		printram("XOVER CMD [%x] = %x\n", 0x100 * channel + 0x320c,
			 reg);
		MCHBAR32(0x100 * channel + 0x320c) = reg;
	}
}

static void dram_timing_regs(ramctr_timing * ctrl)
{
	u32 reg, addr, val32, cpu, stretch;
	struct cpuid_result cpures;
	int channel;

	FOR_ALL_CHANNELS {
		// DBP
		reg = 0;
		reg |= ctrl->tRCD;
		reg |= (ctrl->tRP << 4);
		reg |= (ctrl->CAS << 8);
		reg |= (ctrl->CWL << 12);
		reg |= (ctrl->tRAS << 16);
		printram("DBP [%x] = %x\n", 0x400 * channel + 0x4000, reg);
		MCHBAR32(0x400 * channel + 0x4000) = reg;

		// RAP
		reg = 0;
		reg |= ctrl->tRRD;
		reg |= (ctrl->tRTP << 4);
		reg |= (ctrl->tCKE << 8);
		reg |= (ctrl->tWTR << 12);
		reg |= (ctrl->tFAW << 16);
		reg |= (ctrl->tWR << 24);
		reg |= (3 << 30);
		printram("RAP [%x] = %x\n", 0x400 * channel + 0x4004, reg);
		MCHBAR32(0x400 * channel + 0x4004) = reg;

		// OTHP
		addr = 0x400 * channel + 0x400c;
		reg = 0;
		reg |= ctrl->tXPDLL;
		reg |= (ctrl->tXP << 5);
		reg |= (ctrl->tAONPD << 8);
		reg |= 0xa0000;
		printram("OTHP [%x] = %x\n", addr, reg);
		MCHBAR32(addr) = reg;

		MCHBAR32(0x400 * channel + 0x4014) = 0;

		MCHBAR32(addr) |= 0x00020000;

		// ODT stretch
		reg = 0;

		cpures = cpuid(0);
		cpu = cpures.eax;
		if (IS_IVY_CPU(cpu)
		    || (IS_SANDY_CPU(cpu) && IS_SANDY_CPU_D2(cpu))) {
			stretch = 2;
			addr = 0x400 * channel + 0x400c;
			printram("ODT stretch [%x] = %x\n",
			       0x400 * channel + 0x400c, reg);
			reg = MCHBAR32(addr);

			if (((ctrl->rankmap[channel] & 3) == 0)
			    || (ctrl->rankmap[channel] & 0xc) == 0) {

				// Rank 0 - operate on rank 2
				reg = (reg & ~0xc0000) | (stretch << 18);

				// Rank 2 - operate on rank 0
				reg = (reg & ~0x30000) | (stretch << 16);

				printram("ODT stretch [%x] = %x\n", addr, reg);
				MCHBAR32(addr) = reg;
			}

		} else if (IS_SANDY_CPU(cpu) && IS_SANDY_CPU_C(cpu)) {
			stretch = 3;
			addr = 0x400 * channel + 0x401c;
			reg = MCHBAR32(addr);

			if (((ctrl->rankmap[channel] & 3) == 0)
			    || (ctrl->rankmap[channel] & 0xc) == 0) {

				// Rank 0 - operate on rank 2
				reg = (reg & ~0x3000) | (stretch << 12);

				// Rank 2 - operate on rank 0
				reg = (reg & ~0xc00) | (stretch << 10);

				printram("ODT stretch [%x] = %x\n", addr, reg);
				MCHBAR32(addr) = reg;
			}
		} else {
			stretch = 0;
		}

		// REFI
		reg = 0;
		val32 = ctrl->tREFI;
		reg = (reg & ~0xffff) | val32;
		val32 = ctrl->tRFC;
		reg = (reg & ~0x1ff0000) | (val32 << 16);
		val32 = (u32) (ctrl->tREFI * 9) / 1024;
		reg = (reg & ~0xfe000000) | (val32 << 25);
		printram("REFI [%x] = %x\n", 0x400 * channel + 0x4298,
		       reg);
		MCHBAR32(0x400 * channel + 0x4298) = reg;

		MCHBAR32(0x400 * channel + 0x4294) |= 0xff;

		// SRFTP
		reg = 0;
		val32 = tDLLK;
		reg = (reg & ~0xfff) | val32;
		val32 = ctrl->tXSOffset;
		reg = (reg & ~0xf000) | (val32 << 12);
		val32 = tDLLK - ctrl->tXSOffset;
		reg = (reg & ~0x3ff0000) | (val32 << 16);
		val32 = ctrl->tMOD - 8;
		reg = (reg & ~0xf0000000) | (val32 << 28);
		printram("SRFTP [%x] = %x\n", 0x400 * channel + 0x42a4,
		       reg);
		MCHBAR32(0x400 * channel + 0x42a4) = reg;
	}
}

static void dram_dimm_mapping(ramctr_timing *ctrl)
{
	u32 reg, val32;
	int channel;
	dimm_info *info = &ctrl->info;

	FOR_ALL_CHANNELS {
		dimm_attr *dimmA = 0;
		dimm_attr *dimmB = 0;
		reg = 0;
		val32 = 0;
		if (info->dimm[channel][0].size_mb >=
		    info->dimm[channel][1].size_mb) {
			// dimm 0 is bigger, set it to dimmA
			dimmA = &info->dimm[channel][0];
			dimmB = &info->dimm[channel][1];
			reg |= (0 << 16);
		} else {
			// dimm 1 is bigger, set it to dimmA
			dimmA = &info->dimm[channel][1];
			dimmB = &info->dimm[channel][0];
			reg |= (1 << 16);
		}
		// dimmA
		if (dimmA && (dimmA->ranks > 0)) {
			val32 = dimmA->size_mb / 256;
			reg = (reg & ~0xff) | val32;
			val32 = dimmA->ranks - 1;
			reg = (reg & ~0x20000) | (val32 << 17);
			val32 = (dimmA->width / 8) - 1;
			reg = (reg & ~0x80000) | (val32 << 19);
		}
		// dimmB
		if (dimmB && (dimmB->ranks > 0)) {
			val32 = dimmB->size_mb / 256;
			reg = (reg & ~0xff00) | (val32 << 8);
			val32 = dimmB->ranks - 1;
			reg = (reg & ~0x40000) | (val32 << 18);
			val32 = (dimmB->width / 8) - 1;
			reg = (reg & ~0x100000) | (val32 << 20);
		}
		reg = (reg & ~0x200000) | (1 << 21);	// rank interleave
		reg = (reg & ~0x400000) | (1 << 22);	// enhanced interleave

		// Save MAD-DIMM register
		if ((dimmA && (dimmA->ranks > 0))
		    || (dimmB && (dimmB->ranks > 0))) {
			ctrl->mad_dimm[channel] = reg;
		} else {
			ctrl->mad_dimm[channel] = 0;
		}
	}
}

static void dram_dimm_set_mapping(ramctr_timing * ctrl)
{
	int channel;
	FOR_ALL_CHANNELS {
		MCHBAR32(0x5004 + channel * 4) = ctrl->mad_dimm[channel];
	}
}

static void dram_zones(ramctr_timing * ctrl, int training)
{
	u32 reg, ch0size, ch1size;
	u8 val;
	reg = 0;
	val = 0;
	if (training) {
		ch0size = ctrl->channel_size_mb[0] ? 256 : 0;
		ch1size = ctrl->channel_size_mb[1] ? 256 : 0;
	} else {
		ch0size = ctrl->channel_size_mb[0];
		ch1size = ctrl->channel_size_mb[1];
	}

	if (ch0size >= ch1size) {
		reg = MCHBAR32(0x5014);
		val = ch1size / 256;
		reg = (reg & ~0xff000000) | val << 24;
		reg = (reg & ~0xff0000) | (2 * val) << 16;
		MCHBAR32(0x5014) = reg;
		MCHBAR32(0x5000) = 0x24;
	} else {
		reg = MCHBAR32(0x5014);
		val = ch0size / 256;
		reg = (reg & ~0xff000000) | val << 24;
		reg = (reg & ~0xff0000) | (2 * val) << 16;
		MCHBAR32(0x5014) = reg;
		MCHBAR32(0x5000) = 0x21;
	}
}

static void dram_memorymap(ramctr_timing * ctrl, int me_uma_size)
{
	u32 reg, val, reclaim;
	u32 tom, gfxstolen, gttsize;
	size_t tsegsize, mmiosize, toludbase, touudbase, gfxstolenbase, gttbase,
	    tsegbase, mestolenbase;
	size_t tsegbasedelta, remapbase, remaplimit;
	uint16_t ggc;

	mmiosize = 0x400;

	ggc = pci_read_config16(NORTHBRIDGE, GGC);
	if (!(ggc & 2)) {
		gfxstolen = ((ggc >> 3) & 0x1f) * 32;
		gttsize = ((ggc >> 8) & 0x3);
	} else {
		gfxstolen = 0;
		gttsize = 0;
	}

	tsegsize = CONFIG_SMM_TSEG_SIZE >> 20;

	tom = ctrl->channel_size_mb[0] + ctrl->channel_size_mb[1];

	mestolenbase = tom - me_uma_size;

	toludbase = MIN(4096 - mmiosize + gfxstolen + gttsize + tsegsize,
			tom - me_uma_size);
	gfxstolenbase = toludbase - gfxstolen;
	gttbase = gfxstolenbase - gttsize;

	tsegbase = gttbase - tsegsize;

	// Round tsegbase down to nearest address aligned to tsegsize
	tsegbasedelta = tsegbase & (tsegsize - 1);
	tsegbase &= ~(tsegsize - 1);

	gttbase -= tsegbasedelta;
	gfxstolenbase -= tsegbasedelta;
	toludbase -= tsegbasedelta;

	// Test if it is possible to reclaim a hole in the ram addressing
	if (tom - me_uma_size > toludbase) {
		// Reclaim is possible
		reclaim = 1;
		remapbase = MAX(4096, tom - me_uma_size);
		remaplimit =
		    remapbase + MIN(4096, tom - me_uma_size) - toludbase - 1;
		touudbase = remaplimit + 1;
	} else {
		// Reclaim not possible
		reclaim = 0;
		touudbase = tom - me_uma_size;
	}

	// Update memory map in pci-e configuration space
	printk(BIOS_DEBUG, "Update PCI-E configuration space:\n");

	// TOM (top of memory)
	reg = pcie_read_config32(PCI_DEV(0, 0, 0), TOM);
	val = tom & 0xfff;
	reg = (reg & ~0xfff00000) | (val << 20);
	printk(BIOS_DEBUG, "PCI(0, 0, 0)[%x] = %x\n", TOM, reg);
	pcie_write_config32(PCI_DEV(0, 0, 0), TOM, reg);

	reg = pcie_read_config32(PCI_DEV(0, 0, 0), TOM + 4);
	val = tom & 0xfffff000;
	reg = (reg & ~0x000fffff) | (val >> 12);
	printk(BIOS_DEBUG, "PCI(0, 0, 0)[%x] = %x\n", TOM + 4, reg);
	pcie_write_config32(PCI_DEV(0, 0, 0), TOM + 4, reg);

	// TOLUD (top of low used dram)
	reg = pcie_read_config32(PCI_DEV(0, 0, 0), TOLUD);
	val = toludbase & 0xfff;
	reg = (reg & ~0xfff00000) | (val << 20);
	printk(BIOS_DEBUG, "PCI(0, 0, 0)[%x] = %x\n", TOLUD, reg);
	pcie_write_config32(PCI_DEV(0, 0, 0), TOLUD, reg);

	// TOUUD LSB (top of upper usable dram)
	reg = pcie_read_config32(PCI_DEV(0, 0, 0), TOUUD);
	val = touudbase & 0xfff;
	reg = (reg & ~0xfff00000) | (val << 20);
	printk(BIOS_DEBUG, "PCI(0, 0, 0)[%x] = %x\n", TOUUD, reg);
	pcie_write_config32(PCI_DEV(0, 0, 0), TOUUD, reg);

	// TOUUD MSB
	reg = pcie_read_config32(PCI_DEV(0, 0, 0), TOUUD + 4);
	val = touudbase & 0xfffff000;
	reg = (reg & ~0x000fffff) | (val >> 12);
	printk(BIOS_DEBUG, "PCI(0, 0, 0)[%x] = %x\n", TOUUD + 4, reg);
	pcie_write_config32(PCI_DEV(0, 0, 0), TOUUD + 4, reg);

	if (reclaim) {
		// REMAP BASE
		pcie_write_config32(PCI_DEV(0, 0, 0), REMAPBASE, remapbase << 20);
		pcie_write_config32(PCI_DEV(0, 0, 0), REMAPBASE + 4, remapbase >> 12);

		// REMAP LIMIT
		pcie_write_config32(PCI_DEV(0, 0, 0), REMAPLIMIT, remaplimit << 20);
		pcie_write_config32(PCI_DEV(0, 0, 0), REMAPLIMIT + 4, remaplimit >> 12);
	}

	// TSEG
	reg = pcie_read_config32(PCI_DEV(0, 0, 0), TSEG);
	val = tsegbase & 0xfff;
	reg = (reg & ~0xfff00000) | (val << 20);
	printk(BIOS_DEBUG, "PCI(0, 0, 0)[%x] = %x\n", TSEG, reg);
	pcie_write_config32(PCI_DEV(0, 0, 0), TSEG, reg);

	// GFX stolen memory
	reg = pcie_read_config32(PCI_DEV(0, 0, 0), BDSM);
	val = gfxstolenbase & 0xfff;
	reg = (reg & ~0xfff00000) | (val << 20);
	printk(BIOS_DEBUG, "PCI(0, 0, 0)[%x] = %x\n", BDSM, reg);
	pcie_write_config32(PCI_DEV(0, 0, 0), BDSM, reg);

	// GTT stolen memory
	reg = pcie_read_config32(PCI_DEV(0, 0, 0), BGSM);
	val = gttbase & 0xfff;
	reg = (reg & ~0xfff00000) | (val << 20);
	printk(BIOS_DEBUG, "PCI(0, 0, 0)[%x] = %x\n", BGSM, reg);
	pcie_write_config32(PCI_DEV(0, 0, 0), BGSM, reg);

	if (me_uma_size) {
		reg = pcie_read_config32(PCI_DEV(0, 0, 0), MESEG_LIMIT + 4);
		val = (0x80000 - me_uma_size) & 0xfffff000;
		reg = (reg & ~0x000fffff) | (val >> 12);
		printk(BIOS_DEBUG, "PCI(0, 0, 0)[%x] = %x\n", MESEG_LIMIT + 4, reg);
		pcie_write_config32(PCI_DEV(0, 0, 0), MESEG_LIMIT + 4, reg);

		// ME base
		reg = pcie_read_config32(PCI_DEV(0, 0, 0), MESEG_BASE);
		val = mestolenbase & 0xfff;
		reg = (reg & ~0xfff00000) | (val << 20);
		printk(BIOS_DEBUG, "PCI(0, 0, 0)[%x] = %x\n", MESEG_BASE, reg);
		pcie_write_config32(PCI_DEV(0, 0, 0), MESEG_BASE, reg);

		reg = pcie_read_config32(PCI_DEV(0, 0, 0), MESEG_BASE + 4);
		val = mestolenbase & 0xfffff000;
		reg = (reg & ~0x000fffff) | (val >> 12);
		printk(BIOS_DEBUG, "PCI(0, 0, 0)[%x] = %x\n", MESEG_BASE + 4, reg);
		pcie_write_config32(PCI_DEV(0, 0, 0), MESEG_BASE + 4, reg);

		// ME mask
		reg = pcie_read_config32(PCI_DEV(0, 0, 0), MESEG_LIMIT);
		val = (0x80000 - me_uma_size) & 0xfff;
		reg = (reg & ~0xfff00000) | (val << 20);
		reg = (reg & ~0x400) | (1 << 10);	// set lockbit on ME mem

		reg = (reg & ~0x800) | (1 << 11);	// set ME memory enable
		printk(BIOS_DEBUG, "PCI(0, 0, 0)[%x] = %x\n", MESEG_LIMIT, reg);
		pcie_write_config32(PCI_DEV(0, 0, 0), MESEG_LIMIT, reg);
	}
}

static void set_4f8c(void)
{
	//XXX make this CPU revision dependent
	//struct cpuid_result cpures;
	//u32 cpu;

	//cpures = cpuid(0);
	//cpu = (cpures.eax);
	// when to use ?
	MCHBAR32(0x4f8c) = 0x553c3038;
}

static void save_timings(ramctr_timing * ctrl)
{
	struct mrc_data_container *mrcdata;
	int output_len = ALIGN(sizeof (*ctrl), 16);

	/* Save the MRC S3 restore data to cbmem */
	mrcdata = cbmem_add
		(CBMEM_ID_MRCDATA,
		 output_len + sizeof(struct mrc_data_container));

	printk(BIOS_DEBUG, "Relocate MRC DATA from %p to %p (%u bytes)\n",
	       ctrl, mrcdata, output_len);

	mrcdata->mrc_signature = MRC_DATA_SIGNATURE;
	mrcdata->mrc_data_size = output_len;
	mrcdata->reserved = 0;
	memcpy(mrcdata->mrc_data, ctrl, sizeof (*ctrl));

	/* Zero the unused space in aligned buffer. */
	if (output_len > sizeof (*ctrl))
		memset(mrcdata->mrc_data+sizeof (*ctrl), 0,
		       output_len - sizeof (*ctrl));

	mrcdata->mrc_checksum = compute_ip_checksum(mrcdata->mrc_data,
						    mrcdata->mrc_data_size);
}

const u32 lane_registers[] = {
	0x0000, 0x0200, 0x0400, 0x0600,
	0x0800, 0x0a00, 0x0c00, 0x0e00
};

const u32 lane_mapping[] = {
	0x0, 0x8, 0x2, 0xa,
	0x4, 0xc, 0x6, 0xe
};

#define FOR_ALL_LANES for (lane = 0; lane < NUM_LANES; lane++)
#define FOR_ALL_CHANNELS for (channel = 0; channel < NUM_CHANNELS; channel++)
#define FOR_ALL_POPULATED_RANKS for (slotrank = 0; slotrank < NUM_SLOTRANKS; slotrank++) if (ctrl->rankmap[channel] & (1 << slotrank))
#define FOR_ALL_POPULATED_CHANNELS for (channel = 0; channel < NUM_CHANNELS; channel++) if (ctrl->rankmap[channel])

static void wait_4ce0(void)
{
	int i;
	u32 reg32;
	int j = 0;
	do {
		reg32 = 0;
		for (i = 0; i < 8; i++)
			reg32 |= MCHBAR32(0x4ce0);
		j++;
		if (j > 1000) {
			printk(BIOS_DEBUG, "wait_4ce0 failed\n");
			return;
		}

	} while (reg32);
}

static void post_dd26(ramctr_timing *ctrl)
{
	u32 reg32;
	int i;
	printk(BIOS_DEBUG, "post_dd26...");

	// IO reset ?
	MCHBAR32(0x5030) = MCHBAR32(0x5030) | 0x800000;
	do {
		reg32 = MCHBAR32(0x5030) & 0x800000;
	} while (reg32 != 0);
	MCHBAR32(0x4d94) = 0x0000000f;

	/* Clock Gating */
	RCBA32(0x333c) |= 0x4000000;

	MCHBAR32(0x4d90) = 0x0000000f;

	MCHBAR32(0x5030) = MCHBAR32(0x5030) | 0x400000;
	wait_4ce0();
	MCHBAR8(0x4192) = 0x01;
	wait_4ce0();
	MCHBAR32(0x429c) = 0x00000458;

	//XXX: calculate
	// values are from google peppy
	static const u32 values_41c0[] = {
		0x02000498, 0x03000000, 0x01000000, 0x00000d70, 0x00000400
	};

	for (i = 0; i < 5; i++) {
		MCHBAR8(0x4c31) = 0x00;
		MCHBAR32(0x41c0) = values_41c0[i];
		if (i == 4)
			MCHBAR32(0x41c4) = 0x0f00060e;
		else
			MCHBAR32(0x41c4) = 0x0f00000e;

		MCHBAR32(0x419c) = 0x00000003;

		MCHBAR32(0x48a8) |= 0x10002000;
		MCHBAR8(0x48b8) = 0x05;

		do {
			reg32 = MCHBAR32(0x4804);
		} while (reg32 != 0x00030000);

		MCHBAR8(0x48b8) = 0x04;
		MCHBAR32(0x48a8) &= ~0x00002000;
	}

	wait_4ce0();

	MCHBAR32(0x4d94) = 0x00000000;

	printk(BIOS_DEBUG, "done\n");

}


static void post_dd29_test(ramctr_timing *ctrl)
{
	u32 reg32;
	unsigned i, lane;
	u8 lane_result1[16][8];
	u8 lane_result2[16][8];
	u8 lane_result3[16][8];
	u8 reg8;

	printk(BIOS_DEBUG, "post_dd29_test...");


	FOR_ALL_LANES {
		MCHBAR32(lane_registers[lane] + 0x64) = 0x0003f060 | lane_mapping[lane];
	}

	for (i = 0; i < 0x10; i++) {
		reg8 = (0x78 + i) & 0x7f;
		MCHBAR32(0x3670) = reg8 << 24;

		MCHBAR32(0x3074) |= 0x00100000;

		// test 1
		MCHBAR32(0x364c) = 0xffffffff;
		MCHBAR32(0x3074) = 0x05100008;

		wait_4ce0();

		FOR_ALL_LANES {
			lane_result1[i][lane] =
					  MCHBAR32(lane_registers[lane] + 0x54) & 0xff;
		}

		// test 2
		MCHBAR32(0x3074) = 0x05100000;
		MCHBAR32(0x364c) = 0x11111111;
		MCHBAR32(0x3074) = 0x05100008;

		wait_4ce0();

		FOR_ALL_LANES {
			lane_result2[i][lane] =
					  MCHBAR32(lane_registers[lane] + 0x54) & 0xff;
		}
		MCHBAR32(0x3074) = 0x05100000;
	}

	MCHBAR32(0x364c) = 0x00000000;
	MCHBAR32(0x3074) = 0x05100000;

	FOR_ALL_LANES {
		MCHBAR32(lane_registers[lane] + 0x00)  = 0x0a00c040;
		MCHBAR32(lane_registers[lane] + 0x74) |= 0x00100000;
	}

	for (i = 1; i < 0x10; i++) {
		MCHBAR32(0x364c) = i * 0x11111111;
		MCHBAR32(0x3074) = 0x05100008;

		wait_4ce0();

		FOR_ALL_LANES {
			lane_result3[i][lane] =
					  MCHBAR32(lane_registers[lane] + 0x54) & 0xff;
		}
		MCHBAR32(0x3074) = 0x05100000;
	}

	// XXX: calculate sane values and program
	// values are from google peppy
	u32 lane_timings[] = {
			0x8a889aaa,
			0x99989998,
			0x9aa9a9aa,
			0x978a99a9,
			0x9999a999,
			0xa9aaa9a9,
			0xcbba8998,
			0x999bba89
	};

	FOR_ALL_LANES {
		// XXX: calculate sane values and program
		reg32 = lane_timings[lane];
		MCHBAR32(lane_registers[lane] + 0x4c) = reg32;
		MCHBAR32(0x3074) |= 0x00100000;
	}

	FOR_ALL_LANES {
		MCHBAR32(lane_registers[lane] + 0x64) = 0x0003f000 | lane_mapping[lane];
	}
	MCHBAR32(0x3074) = 0x04000000;
	MCHBAR32(0x5030) |= 0x00800000;

	printk(BIOS_DEBUG, "done\n");
}

static void post_dd2b_test(ramctr_timing *ctrl)
{
	u32 reg32;
	int i, lane;
	u8 lane_result[16][8];
	printk(BIOS_DEBUG, "post_dd2b_test...");

	MCHBAR8(0x4198) = 0x00;
	MCHBAR32(0x4980) = 0x00000040;
	MCHBAR32(0x48a8) = 0x11001800;
	MCHBAR32(0x48ac) = 0x00000002;
	MCHBAR8(0x48b8) = 0x04;
	MCHBAR32(0x4808) = 0x08400882;
	MCHBAR32(0x480c) = 0x08000882;
	MCHBAR32(0x48d8) = 0x00000000;
	MCHBAR32(0x48dc) = 0x00000000;
	MCHBAR32(0x48e8) = 0x000003f8;
	MCHBAR32(0x48ec) = 0x00000000;
	MCHBAR32(0x4908) = 0x00000000;
	MCHBAR32(0x4910) = 0x00080008;
	MCHBAR32(0x4914) = 0x80080020;
	MCHBAR32(0x4200) = 0x00090005;
	MCHBAR32(0x4040) = 0x00008092;
	MCHBAR32(0x4084) = 0x00000000;
	MCHBAR32(0x4098) = 0xffff0001;
	MCHBAR32(0x40d8) = 0x00000000;
	MCHBAR32(0x40dc) = 0x00000000;
	MCHBAR8(0x409c) = 0x00;
	MCHBAR32(0x48b0) = 0x00000000;
	MCHBAR8(0x4c31) = 0x00;
	MCHBAR8(0x41bc) = 0x00;
	MCHBAR32(0x41c0) = 0x00000000;
	MCHBAR32(0x41c4) = 0x0f00060e;
	MCHBAR32(0x419c) = 0x00000007;

	MCHBAR32(0x48a8) = ((MCHBAR32(0x48a8) & ~0x00001800) | 0x00002000);

	MCHBAR8(0x48b8) = 0x05;
	do {
		reg32 = MCHBAR32(0x4804);
	} while (!reg32);
	MCHBAR32(0x48a8) = 0x11001800;

	FOR_ALL_LANES {
		MCHBAR32(lane_registers[lane] + 0x64) = 0x0003f040 | lane_mapping[lane];
	}
	MCHBAR32(0x3074) = 0x05000004;
	MCHBAR32(0x4028) = 0x00170000;
	MCHBAR32(0x4930) = 0x00000000;
	MCHBAR8(0x48ef) = 0x00;
	MCHBAR32(0x48a8) |= 0x11001800;
	MCHBAR8(0x4024) = 0x33;
	MCHBAR32(0x4934) = 0x00000000;
	MCHBAR32(0x48b0) &= ~0x00000000; // XXX

	for (i = 0x18; i < 0x98; i += 0x8) {
		FOR_ALL_LANES {
			MCHBAR32(lane_registers[lane] + 0x00)  = 0x0a00c100 | i;
			MCHBAR32(lane_registers[lane] + 0x74) |= 0x00100000;
		}
		if (MCHBAR32(0x4098) & 1)
		{
			//skip over next channel ?
		}
		MCHBAR32(0x5030) |= 0x00800000;
		MCHBAR32(0x4800) = 5;
		do {
			reg32 = MCHBAR32(0x4804) & 1;
		} while (!reg32);

		FOR_ALL_LANES {
			lane_result[i][lane] =
					  !!(MCHBAR32(lane_registers[lane] + 0x54));
		}
	}

	// program lane registers
	if (0) {
		FOR_ALL_LANES {
		reg32 = 0;
		MCHBAR32(lane_registers[lane] + 0x00)  = 0x0a00c100 | reg32;
		MCHBAR32(lane_registers[lane] + 0x74) |= 0x00100000;
		}
	}

	//XXX: calculate
	// values are from google peppy
	u32 lane_timings[] = {
		0x20, 0x18, 0x1c, 0x18
	};
	for (lane = 0; lane < 4; lane++) {
		// XXX: ref. code programs only 4 lane registers ???
		// values doesn't make any sense
		reg32 = 0;
		MCHBAR32(lane_registers[lane] + 0x00)  = 0x0a00c100 | lane_timings[lane];
		MCHBAR32(lane_registers[lane] + 0x74) |= 0x00100000;
	}
	printk(BIOS_DEBUG, "done\n");
}

static int try_init_dram_ddr3(ramctr_timing *ctrl, int fast_boot,
		int s3_resume, int me_uma_size)
{
	int lane;

	//int err;

	printk(BIOS_DEBUG, "Starting RAM training (%d).\n", fast_boot);
	//POST: dd00

	if (!fast_boot) {
		/* Find fastest common supported parameters */
		dram_find_common_params(ctrl);

		dram_dimm_mapping(ctrl);
	}

	//POST: dd23

	/* Set MCU frequency */
	dram_freq(ctrl);

	if (!fast_boot) {
		/* Calculate timings */
		dram_timing(ctrl);
	}

	MCHBAR32(0x2008) = 0x400;

	/* Set version register */
	MCHBAR32(0x5034) = 0x01060102;

	/* static registers */
	MCHBAR32(0x1800) = 0x00000001;
	MCHBAR32(0x1c20) = 0x00000031;
	MCHBAR32(0x1220) = 0x00000001;
	MCHBAR32(0x3600) = 0x0200c040;
	MCHBAR32(0x3610) = 0x88888888;
	MCHBAR32(0x3620) = 0x03b08060;
	MCHBAR32(0x3630) = 0x88888888;
	MCHBAR32(0x3648) = 0x00000000;
	MCHBAR32(0x364c) = 0x88888888;
	MCHBAR32(0x3670) = 0x00000000;
	MCHBAR32(0x365c) = 0x00000000;
	MCHBAR32(0x3674) = 0x04000000;
	MCHBAR32(0x3660) = 0x07efd810;

	FOR_ALL_LANES {
		MCHBAR32(lane_registers[lane] + 0x64) = 0x0003f000 | lane_mapping[lane];
	}

	MCHBAR32(0x366c) = 0x004d8164;
	MCHBAR32(0x3a24) = 0x004d8164;

	MCHBAR32(0x0f68) = 0x0002051c;
	MCHBAR32(0x3678) = 0x03e00000;
	MCHBAR32(0x1810) = 0x00000010; // COMP1

	/* Enable crossover */
	if (0)
		dram_xover(ctrl);
	{
		MCHBAR32(0x320c) = 0x0001a010;
	}

	MCHBAR32(0x121c) = 0x0013a010;
	MCHBAR32(0x1c1c) = 0x40120010;

	MCHBAR32(0x3208) = 0x0c183060;
	MCHBAR32(0x1208) = 0x0c183060;

	MCHBAR32(0x3418) = 0x08102040;
	MCHBAR32(0x180c) = 0x08102040;

	MCHBAR32(0x3204) = 0;
	MCHBAR32(0x3414) = 0;
	MCHBAR32(0x1808) = 0;
	MCHBAR32(0x3a14) = 0xe0000008;

	MCHBAR32(0x3a18) = 0x000d94ba;
	MCHBAR32(0x3a1c) = 0x0009ca51;

	MCHBAR32(0x2008) = MCHBAR32(0x2008) | 0x316;
	MCHBAR32(0x2000) = 0;
	MCHBAR32(0x2004) = 0;

	MCHBAR8(0x42a0) = 1;
	MCHBAR8(0x46a0) = 0;

	printk(BIOS_DEBUG, "FORCE RCOMP and wait 20us...");
	MCHBAR32(0x5f08) = 0x00000115; //FORCE RCOMP
	udelay(20);
	printk(BIOS_DEBUG, "done\n");

	/* dynamic registers */
	MCHBAR32(0x3644) = (MCHBAR32(0x3644) & ~0x40000000)| 0x20000000;
	MCHBAR32(0x3700) = (MCHBAR32(0x3700) & ~0x04000000)| 0x02000000;
	MCHBAR32(0x3810) = (MCHBAR32(0x3810) & ~0x04000000)| 0x02000000;
	MCHBAR32(0x3904) = (MCHBAR32(0x3904) & ~0x02000000)| 0x01000000;
	MCHBAR32(0x3a04) = (MCHBAR32(0x3a04) & ~0x40000000)| 0x20000000;
	MCHBAR32(0x3a08) = (MCHBAR32(0x3a08) & ~0x04000000)| 0x02000000;
	MCHBAR32(0x3a0c) = (MCHBAR32(0x3a0c) & ~0x04000000)| 0x02000000;
	MCHBAR32(0x3a10) = (MCHBAR32(0x3a10) & ~0x02000000)| 0x01000000;
	MCHBAR32(0x3a20) = (MCHBAR32(0x3a20) & ~0x04000000)| 0x02000000;

	/* static registers */
	MCHBAR32(0x2008) = 0x00000736;
	MCHBAR32(0x3a14) = 0xe00007f8;
	MCHBAR32(0x58a4) = 0x00000001;

	/* Set timing and refresh registers */
	if (0)
		dram_timing_regs(ctrl);
	{
		MCHBAR32(0x4000) = 0x1986716b;
		MCHBAR32(0x4014) = 0x0000d50b;
		MCHBAR32(0x4004) = 0xe8334204;
		MCHBAR32(0x4008) = 0xb0ef1114;
		MCHBAR8(0x40d0) = 0xaa;
		MCHBAR32(0x400c) = 0x0b5addd4;
		MCHBAR32(0x4298) = 0x6cd01860;
		MCHBAR32(0x42a4) = 0x41008200;
		MCHBAR32(0x4290) = 0x00004290;
		MCHBAR32(0x4294) = MCHBAR32(0x4294) | 0xf0;
	}

	/* Set scheduler parameters */
	MCHBAR32(0x4c20) = 0x00102000;

	/* Set cpu specific register */
	set_4f8c();

	/* Set MAD-DIMM registers */
	dram_dimm_set_mapping(ctrl);
	printk(BIOS_DEBUG, "Done dimm mapping\n");

	/* Zone config */
	dram_zones(ctrl, 1);

	// POST dd24

	/* Set memory map */
	dram_memorymap(ctrl, me_uma_size);
	printk(BIOS_DEBUG, "Done memory map\n");

	/* Set IO registers */
	post_dd26(ctrl);

	post_dd2b_test(ctrl);
	post_dd29_test(ctrl);

	return 0;
}

static void init_dram_ddr3(struct pei_data *pei_data, int min_tck)
{
	int me_uma_size;
	int cbmem_was_inited;
	ramctr_timing ctrl;
	int fast_boot;
	struct mrc_data_container *mrc_cache;
	ramctr_timing *ctrl_cached;
	int err;
	spd_raw_data *spds = pei_data->spd_data;
	int mobile = !pei_data->system_type;
	int s3resume = pei_data->boot_mode == 2;
	report_platform_info();

	/* Wait for ME to be ready */
	intel_early_me_init();
	me_uma_size = intel_early_me_uma_size();

	printk(BIOS_DEBUG, "Starting native Platform init\n");

	wait_txt_clear();

	//wrmsr(0x000002e6, (msr_t) { .lo = 0, .hi = 0 });

	memset(&ctrl, 0, sizeof (ctrl));

	early_pch_systemagent(pei_data);

	/* try to find timings in MRC cache */
	mrc_cache = find_current_mrc_cache();
	if (!mrc_cache || (mrc_cache->mrc_data_size < sizeof(ctrl))) {
		if (s3resume) {
			/* Failed S3 resume, reset to come up cleanly */
			outb(0x6, 0xcf9);
			halt();
		}
		ctrl_cached = NULL;
	} else {
		ctrl_cached = (ramctr_timing *)mrc_cache->mrc_data;
	}

	/* verify MRC cache for fast boot */
	if (ctrl_cached) {
		/* check SPD CRC16 to make sure the DIMMs haven't been replaced */
		fast_boot = verify_crc16_spds_ddr3(spds, ctrl_cached);
		if (!fast_boot)
			printk(BIOS_DEBUG, "Stored timings CRC16 mismatch.\n");
		if (!fast_boot && s3resume) {
			/* Failed S3 resume, reset to come up cleanly */
			outb(0x6, 0xcf9);
			halt();
		}
	} else
		fast_boot = 0;

	if (fast_boot) {
		printk(BIOS_DEBUG, "Trying stored timings.\n");
		memcpy(&ctrl, ctrl_cached, sizeof(ctrl));

		err = try_init_dram_ddr3(&ctrl, fast_boot, s3resume, me_uma_size);
		if (err) {
			if (s3resume) {
				/* Failed S3 resume, reset to come up cleanly */
				outb(0x6, 0xcf9);
				halt();
			}
			/* no need to erase bad mrc cache here, it gets overwritten on
			 * successful boot. */
			printk(BIOS_ERR, "Stored timings are invalid !\n");
			fast_boot = 0;
		}
	}
	if (!fast_boot) {
		ctrl.mobile = mobile;
		ctrl.tCK = min_tck;

		/* Get DDR3 SPD data */
		dram_find_spds_ddr3(spds, &ctrl);

		err = try_init_dram_ddr3(&ctrl, fast_boot, s3resume, me_uma_size);
	}
	if (err)
		die("raminit failed");

	//MCHBAR32(0x5024) |= 0x00a030ce;

	//set_scrambling_seed(&ctrl);

	//set_42a0(&ctrl);

	//final_registers(&ctrl);

	/* Zone config */

	//dram_zones(&ctrl, 0);

	if (!fast_boot)
		quick_ram_check();

	intel_early_me_status();
	intel_early_me_init_done(ME_INIT_STATUS_SUCCESS);
	intel_early_me_status();

	report_memory_config();

	cbmem_was_inited = !cbmem_recovery(s3resume);
	if (!fast_boot)
		save_timings(&ctrl);
	if (s3resume && !cbmem_was_inited) {
		/* Failed S3 resume, reset to come up cleanly */
		outb(0x6, 0xcf9);
		halt();
	}

	fill_smbios17(&ctrl);
}

#define HOST_BRIDGE	PCI_DEVFN(0, 0)
#define DEFAULT_TCK	TCK_800MHZ
#if 0
static unsigned int get_mem_min_tck(void)
{
	u32 reg32;
	u8 rev;
	const struct device *dev;
	const struct northbridge_intel_sandybridge_config *cfg = NULL;

	dev = dev_find_slot(0, HOST_BRIDGE);
	if (dev)
		cfg = dev->chip_info;

	/* If this is zero, it just means devicetree.cb didn't set it */
	if (!cfg || cfg->max_mem_clock_mhz == 0) {
		rev = pci_read_config8(PCI_DEV(0, 0, 0), PCI_DEVICE_ID);

		if ((rev & BASE_REV_MASK) == BASE_REV_SNB) {
			/* read Capabilities A Register DMFC bits */
			reg32 = pci_read_config32(PCI_DEV(0, 0, 0), CAPID0_A);
			reg32 &= 0x7;

			switch (reg32) {
			case 7: return TCK_533MHZ;
			case 6: return TCK_666MHZ;
			case 5: return TCK_800MHZ;
			/* reserved: */
			default:
				break;
			}
		} else {
			/* read Capabilities B Register DMFC bits */
			reg32 = pci_read_config32(PCI_DEV(0, 0, 0), CAPID0_B);
			reg32 = (reg32 >> 4) & 0x7;

			switch (reg32) {
			case 7: return TCK_533MHZ;
			case 6: return TCK_666MHZ;
			case 5: return TCK_800MHZ;
			case 4: return TCK_933MHZ;
			case 3: return TCK_1066MHZ;
			case 2: return TCK_1200MHZ;
			case 1: return TCK_1333MHZ;
			/* reserved: */
			default:
				break;
			}
		}
		return DEFAULT_TCK;
	} else {
		if (cfg->max_mem_clock_mhz >= 800)
			return TCK_800MHZ;
		else if (cfg->max_mem_clock_mhz >= 666)
			return TCK_666MHZ;
		else if (cfg->max_mem_clock_mhz >= 533)
			return TCK_533MHZ;
		else
			return TCK_400MHZ;
	}
}
#endif
static unsigned int get_mem_min_tck(void)
{
	return DEFAULT_TCK;
}

void save_mrc_data(struct pei_data *pei_data)
{
	struct mrc_data_container *mrcdata;
	int output_len = ALIGN(pei_data->mrc_output_len, 16);

	/* Save the MRC S3 restore data to cbmem */
	mrcdata = cbmem_add
		(CBMEM_ID_MRCDATA,
		 output_len + sizeof(struct mrc_data_container));

	printk(BIOS_DEBUG, "Relocate MRC DATA from %p to %p (%u bytes)\n",
	       pei_data->mrc_output, mrcdata, output_len);

	mrcdata->mrc_signature = MRC_DATA_SIGNATURE;
	mrcdata->mrc_data_size = output_len;
	mrcdata->reserved = 0;
	memcpy(mrcdata->mrc_data, pei_data->mrc_output,
	       pei_data->mrc_output_len);

	/* Zero the unused space in aligned buffer. */
	if (output_len > pei_data->mrc_output_len)
		memset(mrcdata->mrc_data+pei_data->mrc_output_len, 0,
		       output_len - pei_data->mrc_output_len);

	mrcdata->mrc_checksum = compute_ip_checksum(mrcdata->mrc_data,
						    mrcdata->mrc_data_size);
}

static void prepare_mrc_cache(struct pei_data *pei_data)
{
	struct mrc_data_container *mrc_cache;

	// preset just in case there is an error
	pei_data->mrc_input = NULL;
	pei_data->mrc_input_len = 0;

	if ((mrc_cache = find_current_mrc_cache()) == NULL) {
		/* error message printed in find_current_mrc_cache */
		return;
	}

	pei_data->mrc_input = mrc_cache->mrc_data;
	pei_data->mrc_input_len = mrc_cache->mrc_data_size;

	printk(BIOS_DEBUG, "%s: at %p, size %x checksum %04x\n",
	       __func__, pei_data->mrc_input,
	       pei_data->mrc_input_len, mrc_cache->mrc_checksum);
}

/**
 * Find PEI executable in coreboot filesystem and execute it.
 *
 * @param pei_data: configuration data for UEFI PEI reference code
 */
void sdram_initialize(struct pei_data *pei_data)
{
	printk(BIOS_DEBUG, "Starting Native RAM Init\n");

	/*
	 * Do not pass MRC data in for recovery mode boot,
	 * Always pass it in for S3 resume.
	 */
	if (!recovery_mode_enabled() || pei_data->boot_mode == 2)
		prepare_mrc_cache(pei_data);

	/* If MRC data is not found we cannot continue S3 resume. */
	if (pei_data->boot_mode == 2 && !pei_data->mrc_input) {
		post_code(POST_RESUME_FAILURE);
		printk(BIOS_DEBUG, "Giving up in sdram_initialize: "
		       "No MRC data\n");
		outb(0x6, 0xcf9);
		halt();
	}

	init_dram_ddr3(pei_data, get_mem_min_tck());

	/* For reference print the System Agent version
	 * after executing the UEFI PEI stage.
	 */
	u32 version = MCHBAR32(0x5034);
	printk(BIOS_DEBUG, "System Agent Version %d.%d.%d Build %d\n",
		version >> 24 , (version >> 16) & 0xff,
		(version >> 8) & 0xff, version & 0xff);

	report_memory_config();
}

