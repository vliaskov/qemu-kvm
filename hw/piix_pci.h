/*
 * piix_pci.h
 *
 * Copyright (c) 2009 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 * Copyright (C) 2012 Jason Baron <jbaron@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#ifndef HW_I440FX_H
#define HW_I440FX_H

#include "hw.h"
#include "range.h"
#include "isa.h"
#include "sysbus.h"
#include "pc.h"
#include "apm.h"
#include "apic.h"
#include "pci.h"
#include "pcie_host.h"
#include "acpi.h"
#include "dimm.h"

/*
 * I440FX chipset data sheet.
 * http://download.intel.com/design/chipsets/datashts/29054901.pdf
 */
#define I440FX_PCI_HOLE_START 0xe0000000

#define TYPE_I440FX_HOST_DEVICE "i440FX-pcihost"
#define I440FX_HOST_DEVICE(obj) \
     OBJECT_CHECK(I440FXState, (obj), TYPE_I440FX_HOST_DEVICE)

#define TYPE_I440FX_PCI_DEVICE "i440FX"
#define I440FX_PCI_DEVICE(obj) \
     OBJECT_CHECK(PCII440FXState, (obj), TYPE_I440FX_PCI_DEVICE)

typedef struct PAMMemoryRegion {
    MemoryRegion alias[4];  /* index = PAM value */
    unsigned current;
} PAMMemoryRegion;

typedef struct PCII440FXState {
    PCIDevice dev;
    MemoryRegion *system_memory;
    MemoryRegion *pci_address_space;
    MemoryRegion *ram_memory;
    MemoryRegion *address_space_io;
    PAMMemoryRegion pam_regions[13];
    MemoryRegion pci_hole;
    MemoryRegion pci_hole_64bit;
    MemoryRegion smram_region;
    uint8_t smm_enabled;
    ram_addr_t below_4g_mem_size;
    ram_addr_t above_4g_mem_size;
    DimmBus *dram_channel0;
    DimmBus *pv_dram_channel;
    void *fw_cfg;
} PCII440FXState;

typedef struct I440FXState {
    PCIHostState parent_obj;
    PCII440FXState mch;
} I440FXState;

#define PIIX_NUM_PIC_IRQS       16      /* i8259 * 2 */
#define PIIX_NUM_PIRQS          4ULL    /* PIRQ[A-D] */
#define XEN_PIIX_NUM_PIRQS      128ULL
#define PIIX_PIRQC              0x60

typedef struct PIIX3State {
    PCIDevice dev;

    /*
     * bitmap to track pic levels.
     * The pic level is the logical OR of all the PCI irqs mapped to it
     * So one PIC level is tracked by PIIX_NUM_PIRQS bits.
     *
     * PIRQ is mapped to PIC pins, we track it by
     * PIIX_NUM_PIRQS * PIIX_NUM_PIC_IRQS = 64 bits with
     * pic_irq * PIIX_NUM_PIRQS + pirq
     */
#if PIIX_NUM_PIC_IRQS * PIIX_NUM_PIRQS > 64
#error "unable to encode pic state in 64bit in pic_levels."
#endif
    uint64_t pic_levels;

    qemu_irq *pic;

    /* This member isn't used. Just for save/load compatibility */
    int32_t pci_irq_levels_vmstate[PIIX_NUM_PIRQS];
} PIIX3State;


int pci_slot_get_pirq(PCIDevice *pci_dev, int pci_intx);
void piix3_set_irq_pic(PIIX3State *piix3, int pic_irq);
void piix3_set_irq(void *opaque, int pirq, int level);
PCIINTxRoute piix3_route_intx_pin_to_irq(void *opaque, int pci_intx);
hwaddr i440fx_pmc_dimm_offset(DeviceState *dev, uint64_t size);

#define I440FX_PAM      0x59
#define I440FX_PAM_SIZE 7
#define I440FX_SMRAM    0x72

#endif /* HW_I440FX_H */
