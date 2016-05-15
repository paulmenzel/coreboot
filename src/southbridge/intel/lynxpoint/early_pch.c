/*
 * This file is part of the coreboot project.
 *
 * Copyright (C) 2012 Google Inc.
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
#include <arch/io.h>
#include <device/device.h>
#include <device/pci_def.h>
#include <timestamp.h>
#include <cpu/x86/tsc.h>
#include <elog.h>
#include <string.h>
#include "northbridge/intel/haswell/pei_data.h"
#include "northbridge/intel/haswell/haswell.h"
#include "pch.h"
#include "chip.h"
#include "me.h"
#include "iobp.h"

#if CONFIG_INTEL_LYNXPOINT_LP
#include "lp_gpio.h"
#else
#include "southbridge/intel/common/gpio.h"
#endif

const struct rcba_config_instruction pch_early_config[] = {
	/* Enable IOAPIC */
	RCBA_SET_REG_16(OIC, 0x0100),
	/* PCH BWG says to read back the IOAPIC enable register */
	RCBA_READ_REG_16(OIC),

	RCBA_END_CONFIG,
};

static void bar_reg_clear_set(u8 *bar, u16 reg, u32 clear, u32 set)
{
	u32 tmp;

	tmp = read32(bar + reg);
	tmp &= ~clear;
	tmp |= set;
	write32(bar + reg, tmp);
}

static void early_pch_init_ehci(struct pei_data* pei_data)
{
	int i;
	u32 reg32;
	u8 *ehci_bar;
	pci_devfn_t dev = PCI_DEV(0, 0x1d, 0);

	ehci_bar = (u8 *)pci_read_config32(dev, PCI_BASE_ADDRESS_0);
	if (!ehci_bar) {
		printk(BIOS_DEBUG, "EHCI2 PCI_BASE_ADDRESS_0 isn't set, skipping init.\n");
		return;
	}
	printk(BIOS_DEBUG, "EHCI2...");

	reg32 = pci_read_config32(dev, PCI_COMMAND);
	reg32 |= PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY;
	pci_write_config32(dev, PCI_COMMAND, reg32);

	/* Unlock read only registers */
	pci_write_config32(dev, 0x80,
			   pci_read_config32(dev, 0x80) | 0x1);

	bar_reg_clear_set(ehci_bar, 0x0004, 0x00000008, 0x00000002);

	/* Lock read only registers */
	pci_write_config32(dev, 0x80,
			   pci_read_config32(dev, 0x80) & ~0x1);

	pch_iobp_update(0xe5007f04, 0, 0x00004481);
	pch_iobp_update(0xe500410f, 0, 0x000f2800);
	pch_iobp_update(0xe500420f, 0, 0x000ea800);
	pch_iobp_update(0xe500430f, 0, 0x000fa800);
	pch_iobp_update(0xe500440f, 0, 0x000fa800);
	pch_iobp_update(0xe500450f, 0, 0x000fa800);
	pch_iobp_update(0xe500460f, 0, 0x000fa800);
	pch_iobp_update(0xe500470f, 0, 0x000fa800);
	pch_iobp_update(0xe500480f, 0, 0x000fa800);
	pch_iobp_update(0xe5007f14, 0, 0x0018fd55);
	pch_iobp_update(0xe5007f02, 0, 0x0a002453);

	/* Set over-current limit */
	reg32 = 0;
	for (i = 0; i < 4; i++) {
		if (!pei_data->usb2_ports[i + 4].enable)
			continue;
		if (pei_data->usb2_ports[i + 4].over_current_pin == USB_OC_PIN_SKIP)
			continue;

		switch (pei_data->usb2_ports[i + 4].over_current_pin) {
		case  8: reg32 |= 0x01 << (i * 8); break;
		case  9: reg32 |= 0x02 << (i * 8); break;
		case 10: reg32 |= 0x04 << (i * 8); break;
		case 11: reg32 |= 0x08 << (i * 8); break;
		case 12: reg32 |= 0x10 << (i * 8); break;
		case 13: reg32 |= 0x20 << (i * 8); break;
		default:
			printk(BIOS_WARNING, "Invalid overcurrent pin mapping %d -> %d\n",
				   i + 4, pei_data->usb2_ports[i + 4].over_current_pin);
			break;
		}
	}

	pci_write_config32(dev, EHCI_OCMAP, reg32);

	/* Write disabled ports */
	reg32 = 0;
	for (i = 0; i < MAX_USB2_PORTS; i++) {
		if (!pei_data->usb2_ports[i].enable)
			reg32 |= (1 << i);
	}
	pci_write_config32(dev, EHCI_PDO, reg32);

	printk(BIOS_DEBUG, "done\n");
}

static void early_pch_init_xhci(struct pei_data* pei_data)
{
	int i;
	u32 reg32;
	u8 reg8;
	u8 *xhci_bar = (u8 *)0xe8100000;
	pci_devfn_t dev = PCI_DEV(0, 0x14, 0);

	//TODO: get maximum supported USB2 & USB3 ports
	// Hardware has registers for 8 / 8
	// mrc.bin assumes 8 / 4
	// pei_data has 16 / 16
	printk(BIOS_DEBUG, "xHCI...");

	/* Activate bar */
	pci_write_config32(dev, PCI_BASE_ADDRESS_0, (u32)xhci_bar);

	reg32 = pci_read_config32(dev, PCI_COMMAND);
	reg32 |= PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY;
	pci_write_config32(dev, PCI_COMMAND, reg32);

	bar_reg_clear_set(xhci_bar, 0x000c, 0x00040001, 0x0200000a);
	bar_reg_clear_set(xhci_bar, 0x0010, 0x00000020, 0x00000600);
	bar_reg_clear_set(xhci_bar, 0x8058, 0x00010100, 0x00000000);
	bar_reg_clear_set(xhci_bar, 0x8060, 0x00000000, 0x0200000a);
	bar_reg_clear_set(xhci_bar, 0x8094, 0x00000000, 0x00a0c000);
	bar_reg_clear_set(xhci_bar, 0x80e0, 0x00020000, 0x00000040);
	bar_reg_clear_set(xhci_bar, 0x80f0, 0x00100000, 0x00000000);
	bar_reg_clear_set(xhci_bar, 0x80fc, 0x00000000, 0x02000000);
	bar_reg_clear_set(xhci_bar, 0x8110, 0x00000104, 0x00100800);
	bar_reg_clear_set(xhci_bar, 0x8140, 0x00010102, 0xf500600c);
	bar_reg_clear_set(xhci_bar, 0x8154, 0x00200000, 0x00002000);
	bar_reg_clear_set(xhci_bar, 0x8164, 0x00000000, 0x00000003);
	bar_reg_clear_set(xhci_bar, 0x8174, 0xffffffff, 0x01400c0a);
	bar_reg_clear_set(xhci_bar, 0x817c, 0x00000000, 0x033200a3);
	bar_reg_clear_set(xhci_bar, 0x8180, 0x00000000, 0x00cb0028);
	bar_reg_clear_set(xhci_bar, 0x8184, 0x00000000, 0x0064001e);

	/* avoid writing top-most byte, it is write once ! */
	reg32 = pci_read_config16(dev, XHCI_XHCC2);
	reg32 |= 0xf0401;
	pci_write_config16(dev, XHCI_XHCC2, reg32);

	reg8 = pci_read_config8(dev, XHCI_XHCC2 + 2);
	reg8 |= 0xf;
	pci_write_config8(dev, XHCI_XHCC2 + 2, reg8);

	bar_reg_clear_set(xhci_bar, 0x8188, 0x00000000, 0x05000000);

	/* XHCI USB2 Overcurrent Pin Mapping 1 */
	reg32 = 0;
	for (i = 0; i < 4; i++) {
		if (!pei_data->usb2_ports[i].enable)
			continue;
		if (pei_data->usb2_ports[i].over_current_pin == USB_OC_PIN_SKIP)
			continue;

		switch (pei_data->usb2_ports[i].over_current_pin) {
		case  0: reg32 |= 0x01 << (i * 8); break;
		case  1: reg32 |= 0x02 << (i * 8); break;
		case  2: reg32 |= 0x04 << (i * 8); break;
		case  3: reg32 |= 0x08 << (i * 8); break;
		case  8: reg32 |= 0x10 << (i * 8); break;
		case  9: reg32 |= 0x20 << (i * 8); break;
		case 12: reg32 |= 0x40 << (i * 8); break;
		case 13: reg32 |= 0x80 << (i * 8); break;
		default:
			printk(BIOS_WARNING, "Invalid overcurrent pin mapping %d -> %d\n",
				   i, pei_data->usb2_ports[i].over_current_pin);
			break;
		}
	}

	pci_write_config32(dev, XHCI_U2OCM1, 1);

	/* Set XHCI USB2 Overcurrent Pin Mapping 2 */
	reg32 = 0;
	for (i = 0; i < 4; i++) {
		if (!pei_data->usb2_ports[i + 4].enable)
			continue;
		if (pei_data->usb2_ports[i + 4].over_current_pin == USB_OC_PIN_SKIP)
			continue;

		switch (pei_data->usb2_ports[i + 4].over_current_pin) {
		case  4: reg32 |= 0x01 << (i * 8); break;
		case  5: reg32 |= 0x02 << (i * 8); break;
		case  6: reg32 |= 0x04 << (i * 8); break;
		case  7: reg32 |= 0x08 << (i * 8); break;
		case 10: reg32 |= 0x10 << (i * 8); break;
		case 11: reg32 |= 0x20 << (i * 8); break;
		default:
			printk(BIOS_WARNING, "Invalid overcurrent pin mapping %d -> %d\n",
				   i + 4, pei_data->usb2_ports[i + 4].over_current_pin);
			break;
		}
	}
	pci_write_config32(dev, XHCI_U2OCM2, 1);

	/* Set XHCI USB3 Overcurrent Pin Mapping 1 */
	reg32 = 0;
	for (i = 0; i < 4; i++) {
		if (!pei_data->usb3_ports[i].enable)
			continue;
		if (pei_data->usb3_ports[i].over_current_pin == USB_OC_PIN_SKIP)
			continue;

		reg32 |= (1 << (i * 8 + pei_data->usb3_ports[i].over_current_pin));
	}
	pci_write_config32(dev, XHCI_U3OCM1, 1);

	/* XHCI USB3 Overcurrent Pin Mapping 2 */
	reg32 = 0;
	for (i = 0; i < 4; i++) {
		if (!pei_data->usb3_ports[i + 4].enable)
			continue;
		if (pei_data->usb3_ports[i + 4].over_current_pin == USB_OC_PIN_SKIP)
			continue;

		reg32 |= (1 << (i * 8 + pei_data->usb3_ports[i].over_current_pin));
	}
	pci_write_config32(dev, XHCI_U3OCM2, 1);

	/* Set USB2 ports on XHCI controller enable */
	reg32 = 0;
	for (i = 0; i < MAX_USB2_PORTS; i++) {
		if (pei_data->usb2_ports[i].enable)
			reg32 |= (1 << i);
	}
	pci_write_config32(dev, XHCI_USB2PRM, reg32);

	/* Write USB2 disabled ports */
	reg32 = 0;
	for (i = 0; i < MAX_USB2_PORTS; i++) {
		if (!pei_data->usb2_ports[i].enable)
			reg32 |= (1 << i);
	}
	pci_write_config32(dev, XHCI_USB2PDO, reg32);

	/* Set USB3 superspeed enable */
	reg32 = 0;
	for (i = 0; i < MAX_USB3_PORTS; i++) {
		if (pei_data->usb3_ports[i].enable)
			reg32 |= (1 << i);
	}
	pci_write_config32(dev, XHCI_USB3PRM, reg32);

	/* Write USB3 superspeed disabled ports */
	reg32 = 0;
	for (i = 0; i < MAX_USB3_PORTS; i++) {
		if (!pei_data->usb3_ports[i].enable)
			reg32 |= (1 << i);
	}
	pci_write_config32(dev, XHCI_USB3PDO, reg32);

	for (i = 0; i < 8; i++) {
		if (pei_data->usb3_ports[i].enable)
			pch_iobp_update(0xe5004100 + i * 0x100, 0, 0x00059e01);
		else
			pch_iobp_update(0xe5004100 + i * 0x100, 0, 0x00059501);
	}

	printk(BIOS_DEBUG, "done\n");
}

static void early_pch_init_sata(void)
{
	u32 reg32;
	pci_devfn_t dev = PCI_DEV(0, 0x1f, 2);

	printk(BIOS_DEBUG, "SATA...");

	/* Note: the ref. code writes the same registers as lynxpoint/sata.c does.
	 * Skip it for now and program IOBPs only. */

	pch_iobp_update(0xea008008, 0, 0x1c00217f);
	pch_iobp_update(0xea002408, 0, 0xea6ce108);
	pch_iobp_update(0xea002608, 0, 0xea6ce108);
	pch_iobp_update(0xea002438, 0, 0x07ce003d);
	pch_iobp_update(0xea002638, 0, 0x07ce003d);
	pch_iobp_update(0xea00242c, 0, 0x0f020140);
	pch_iobp_update(0xea00262c, 0, 0x0f020140);
	pch_iobp_update(0xea002440, 0, 0x01030c00);
	pch_iobp_update(0xea002640, 0, 0x01030c00);
	pch_iobp_update(0xea002410, 0, 0x55514001);
	pch_iobp_update(0xea002610, 0, 0x55514001);
	pch_iobp_update(0xea002550, 0, 0x023f3f3f);
	pch_iobp_update(0xea002750, 0, 0x023f3f3f);
	pch_iobp_update(0xea002554, 0, 0x00020000);
	pch_iobp_update(0xea002754, 0, 0x00020000);
	pch_iobp_update(0xea002540, 0, 0x00140718);
	pch_iobp_update(0xea002740, 0, 0x00140718);
	pch_iobp_update(0xea002544, 0, 0x0014091b);
	pch_iobp_update(0xea002744, 0, 0x0014091b);
	pch_iobp_update(0xea002548, 0, 0x00140918);
	pch_iobp_update(0xea002748, 0, 0x00140918);
	pch_iobp_update(0xea00257c, 0, 0x3fc02400);
	pch_iobp_update(0xea00277c, 0, 0x3fc02400);
	pch_iobp_update(0xea00248c, 0, 0x0c802046);
	pch_iobp_update(0xea00268c, 0, 0x0c802046);
	pch_iobp_update(0xea0024a4, 0, 0x047a8306);
	pch_iobp_update(0xea0026a4, 0, 0x047a8306);
	pch_iobp_update(0xea0024ac, 0, 0x00001020);
	pch_iobp_update(0xea0026ac, 0, 0x00001020);
	pch_iobp_update(0xea002418, 0, 0x38250508);
	pch_iobp_update(0xea002618, 0, 0x38250508);
	pch_iobp_update(0xea002400, 0, 0xcf1f0080);
	pch_iobp_update(0xea002600, 0, 0xcf1f0080);
	pch_iobp_update(0xea002428, 0, 0x580e0000);
	pch_iobp_update(0xea002628, 0, 0x580e0000);
	pch_iobp_update(0xea00241c, 0, 0x00002400);
	pch_iobp_update(0xea00261c, 0, 0x00002400);
	pch_iobp_update(0xea002578, 0, 0x00001880);
	pch_iobp_update(0xea002778, 0, 0x00001880);

	pch_iobp_update(0xea002490, 0, 0x2b3e4c5a);
	pch_iobp_update(0xea002690, 0, 0x2b3e4c5a);
	pch_iobp_update(0xea002778, 0, 0x00001880);
	pch_iobp_update(0xea002778, 0, 0x00001880);
	pch_iobp_update(0xea002778, 0, 0x00001880);

	/* R_PCH_SATA_TM2 - Undocumented in EDS, set according to ref. code */
	reg32 = pci_read_config32(dev, 0x98);
	reg32 |= 0x005c0220;
	pci_write_config32(dev, 0x98, reg32);

	printk(BIOS_DEBUG, "done\n");
}

static void early_pch_init_pcie(void)
{
	printk(BIOS_DEBUG, "PCIe...");

	// set ACPI_BASE_LOCK in ACPI_BASE_LOCK reg
	pci_write_config8(PCH_LPC_DEV, 0xa6,
			    pci_read_config8(PCH_LPC_DEV, 0xa6) | 2);

	RCBA32(0x2088) = 0x00109000;
	read32(DEFAULT_RCBA + 0x20ac);	// !!! = 0x00000000
	RCBA32(0x20ac) = 0x40000000;

	pch_iobp_update(0xe90021cc, 0, 0x38005707);
	pch_iobp_update(0xe90023cc, 0, 0x38005707);
	pch_iobp_update(0xe9002168, 0, 0xba004a2a);
	pch_iobp_update(0xe9002368, 0, 0xba004a2a);
	pch_iobp_update(0xe900216c, 0, 0x0080003f);
	pch_iobp_update(0xe900236c, 0, 0x0080003f);
	pch_iobp_update(0xe900214c, 0, 0x00120598);
	pch_iobp_update(0xe900234c, 0, 0x00120598);
	pch_iobp_update(0xe9002164, 0, 0x00005408);
	pch_iobp_update(0xe9002364, 0, 0x00005408);
	pch_iobp_update(0xe9002170, 0, 0x00000020);
	pch_iobp_update(0xe9002370, 0, 0x00000020);
	pch_iobp_update(0xe9002114, 0, 0x0381191b);
	pch_iobp_update(0xe9002314, 0, 0x0381191b);
	pch_iobp_update(0xe9002038, 0, 0x07ce003b);
	pch_iobp_update(0xe9002238, 0, 0x07ce003b);
	pch_iobp_update(0xe9002014, 0, 0x00006600);
	pch_iobp_update(0xe9002214, 0, 0x00006600);

	pch_iobp_update(0xec000106, 0, 0x00003100);

	/* Don't observe SPI Descriptor Component Section 0 */
	RCBA32(0x38b0) &= ~0x1000;

	printk(BIOS_DEBUG, "done\n");
}

static void early_pch_init_thermal(void)
{
	u8 *thermal_bar = (u8 *)0x40000000;
	pci_devfn_t dev = PCI_DEV(0, 0x1f, 6);

	printk(BIOS_DEBUG, "Thermal...");

	/* Program address for temporary BAR.  */
	pci_write_config32(dev, 0x40, (u32)thermal_bar);
	pci_write_config32(dev, 0x44, 0x0);

	/* Activate temporary BAR.  */
	pci_write_config32(dev, 0x40,
			    pci_read_config32(dev, 0x40) | 5);

	write16(thermal_bar + 0x10, 0x0154);
	write8(thermal_bar + 0x06, 0xff);
	write8(thermal_bar + 0x80, 0xff);
	write8(thermal_bar + 0x84, 0x00);
	write8(thermal_bar + 0x82, 0x00);

#if 0
	//XXX
	/* Perform init.  */
	/* Configure TJmax.  */
	msr = rdmsr(MSR_TEMPERATURE_TARGET);
	write16p(0x40000012, ((msr.lo >> 16) & 0xff) << 6);
#endif

	RCBA32_AND_OR(0x38b0, 0xffff8003, 0x403c);

	write8(thermal_bar + 0xa, 0x01);

	/* Disable temporary BAR.  */
	pci_write_config32(dev, 0x40,
			   pci_read_config32(dev, 0x40) & ~1);
	pci_write_config32(dev, 0x40, 0);

	printk(BIOS_DEBUG, "done\n");
}

static void early_pch_init_me(void)
{
	u32 reg32;
	struct me_did did;
	pci_devfn_t dev = PCI_DEV(0, 0x16, 0);

	printk(BIOS_DEBUG, "Management Engine...");

	intel_early_me_status();

	/* Clear init_done status field */
	reg32 = pci_read_config32(dev, PCI_ME_H_GS);
	memcpy(&did, &reg32, sizeof(reg32));
	did.init_done = 0;
	memcpy(&reg32, &did, sizeof(reg32));
	pci_write_config32(dev, PCI_ME_H_GS, reg32);

	printk(BIOS_DEBUG, "done\n");
}

static void early_pch_init_smbus(void)
{
	u32 reg32;
	u8 reg8;
	pci_devfn_t dev = PCI_DEV(0, 0x1f, 3);

	printk(BIOS_DEBUG, "SMBUS...");

	/* Disable SMBus */
	reg32 = pci_read_config32(dev, SMB_BASE);
	reg32 &= ~PCI_BASE_ADDRESS_SPACE_IO;
	pci_write_config32(dev, SMB_BASE, reg32);

	/* Disable SPD write access:
	 * Writes to addresses 50h - 57h are disabled. */
	reg8 = pci_read_config8(dev, HOSTC);
	reg8 |= (1 << 4);
	pci_write_config8(dev, HOSTC, reg8);

	/* Enable SMBus */
	reg32 = pci_read_config32(dev, SMB_BASE);
	reg32 |= PCI_BASE_ADDRESS_SPACE_IO;
	pci_write_config32(dev, SMB_BASE, reg32);

	printk(BIOS_DEBUG, "done\n");
}

static void early_pch_init_dmi(void)
{
	DMIBAR32(0x071c) = 0x0000000e;
	DMIBAR32(0x0720) = 0x01060100;
	DMIBAR8(0x0a78) |= 0x02;

	DMIBAR32(0x0014) &= ~0x00000004;
	DMIBAR32(0x002c) |= 0x80000004;
	DMIBAR32(0x0038) |= 0x80000000;

	RCBA32_OR(0x0050, 0x000a0000);
	RCBA32_OR(0x2014, 0x0000006a);
	RCBA32_OR(0x2030, 0x82000004);
	RCBA32_OR(0x2040, 0x87000080);
	RCBA32_OR(0x0050, 0x80000000);
}

/* Replaces SystemAgent reference code.
 * Run with native ram init. */
void early_pch_systemagent(void *arg)
{
	struct pei_data* pei_data = arg;

	RCBA32(GCS) = RCBA32(GCS) | (1 << 6);
	RCBA32(GCS) = RCBA32(GCS) & ~(1 << 6);

	RCBA32(0x3310) = 0x00000010;

	printk(BIOS_DEBUG, "Starting native PCH init.\n");

	early_pch_init_sata();
	early_pch_init_me();
	early_pch_init_pcie();
	early_pch_init_thermal();
	early_pch_init_smbus();
	early_pch_init_ehci(pei_data);
	early_pch_init_xhci(pei_data);
	early_pch_init_dmi();

	printk(BIOS_DEBUG, "Native PCH init done.\n");
}

int pch_is_lp(void)
{
	u8 id = pci_read_config8(PCH_LPC_DEV, PCI_DEVICE_ID + 1);
	return id == PCH_TYPE_LPT_LP;
}

static void pch_enable_bars(void)
{
	/* Setting up Southbridge. In the northbridge code. */
	pci_write_config32(PCH_LPC_DEV, RCBA, (uintptr_t)DEFAULT_RCBA | 1);

	pci_write_config32(PCH_LPC_DEV, PMBASE, DEFAULT_PMBASE | 1);
	/* Enable ACPI BAR */
	pci_write_config8(PCH_LPC_DEV, ACPI_CNTL, 0x80);

	pci_write_config32(PCH_LPC_DEV, GPIO_BASE, DEFAULT_GPIOBASE|1);

	/* Enable GPIO functionality. */
	pci_write_config8(PCH_LPC_DEV, GPIO_CNTL, 0x10);
}

static void pch_generic_setup(void)
{
	printk(BIOS_DEBUG, "Disabling Watchdog reboot...");
	RCBA32(GCS) = RCBA32(GCS) | (1 << 5);	/* No reset */
	outw((1 << 11), DEFAULT_PMBASE | 0x60 | 0x08);	/* halt timer */
	printk(BIOS_DEBUG, " done.\n");
}

uint64_t get_initial_timestamp(void)
{
	tsc_t base_time = {
		.lo = pci_read_config32(PCI_DEV(0, 0x00, 0), 0xdc),
		.hi = pci_read_config32(PCI_DEV(0, 0x1f, 2), 0xd0)
	};
	return tsc_to_uint64(base_time);
}

static int sleep_type_s3(void)
{
	u32 pm1_cnt;
	u16 pm1_sts;
	int is_s3 = 0;

	/* Check PM1_STS[15] to see if we are waking from Sx */
	pm1_sts = inw(DEFAULT_PMBASE + PM1_STS);
	if (pm1_sts & WAK_STS) {
		/* Read PM1_CNT[12:10] to determine which Sx state */
		pm1_cnt = inl(DEFAULT_PMBASE + PM1_CNT);
		if (((pm1_cnt >> 10) & 7) == SLP_TYP_S3) {
			/* Clear SLP_TYPE. */
			outl(pm1_cnt & ~(7 << 10), DEFAULT_PMBASE + PM1_CNT);
			is_s3 = 1;
		}
	}
	return is_s3;
}

void pch_enable_lpc(void)
{
	const struct device *dev = dev_find_slot(0, PCI_DEVFN(0x1f, 0));
	const struct southbridge_intel_lynxpoint_config *config = NULL;

	/* Set COM1/COM2 decode range */
	pci_write_config16(PCH_LPC_DEV, LPC_IO_DEC, 0x0010);

	/* Enable SuperIO + MC + COM1 + PS/2 Keyboard/Mouse */
	u16 lpc_config = CNF1_LPC_EN | CNF2_LPC_EN | GAMEL_LPC_EN |
		COMA_LPC_EN | KBC_LPC_EN | MC_LPC_EN;
	pci_write_config16(PCH_LPC_DEV, LPC_EN, lpc_config);

	/* Set up generic decode ranges */
	if (!dev)
		return;
	if (dev->chip_info)
		config = dev->chip_info;
	if (!config)
		return;

	pci_write_config32(PCH_LPC_DEV, LPC_GEN1_DEC, config->gen1_dec);
	pci_write_config32(PCH_LPC_DEV, LPC_GEN2_DEC, config->gen2_dec);
	pci_write_config32(PCH_LPC_DEV, LPC_GEN3_DEC, config->gen3_dec);
	pci_write_config32(PCH_LPC_DEV, LPC_GEN4_DEC, config->gen4_dec);
}

int early_pch_init(const void *gpio_map,
                   const struct rcba_config_instruction *rcba_config)
{
	int wake_from_s3;

	pch_enable_lpc();

	pch_enable_bars();

#if CONFIG_INTEL_LYNXPOINT_LP
	setup_pch_lp_gpios(gpio_map);
#else
	setup_pch_gpios(gpio_map);
#endif

	console_init();

	pch_generic_setup();

	/* Enable SMBus for reading SPDs. */
	enable_smbus();

	/* Early PCH RCBA settings */
	pch_config_rcba(pch_early_config);

	/* Mainboard RCBA settings */
	pch_config_rcba(rcba_config);

	wake_from_s3 = sleep_type_s3();

#if CONFIG_ELOG_BOOT_COUNT
	if (!wake_from_s3)
		boot_count_increment();
#endif

	/* Report if we are waking from s3. */
	return wake_from_s3;
}
