/*
 * QEMU GMCH/ICH9 LPC PM Emulation
 *
 *  Copyright (c) 2009 Isaku Yamahata <yamahata at valinux co jp>
 *                     VA Linux Systems Japan K.K.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 */

#ifndef HW_ACPI_ICH9_H
#define HW_ACPI_ICH9_H

#include "acpi.h"

#define ICH9_MEM_BASE    0xaf80
#define ICH9_MEM_EJ_BASE    0xafa0
#define ICH9_MEM_HOTPLUG_STATUS 8
#define ICH9_MEM_OST_REMOVE_FAIL 0xafa1
#define ICH9_MEM_OST_ADD_SUCCESS 0xafa2
#define ICH9_MEM_OST_ADD_FAIL 0xafa3

typedef struct ICH9LPCPMRegs {
    /*
     * In ich9 spec says that pm1_cnt register is 32bit width and
     * that the upper 16bits are reserved and unused.
     * PM1a_CNT_BLK = 2 in FADT so it is defined as uint16_t.
     */
    ACPIREGS acpi_regs;
    MemoryRegion io;
    MemoryRegion io_gpe;
    MemoryRegion io_smi;
    MemoryRegion io_memhp;
    uint32_t smi_en;
    uint32_t smi_sts;

    qemu_irq irq;      /* SCI */

    struct gpe_regs gperegs;
    uint32_t pm_io_base;
    Notifier powerdown_notifier;
} ICH9LPCPMRegs;

void ich9_pm_init(void *lpc,
                  qemu_irq sci_irq, qemu_irq cmos_s3_resume);
void ich9_pm_iospace_update(ICH9LPCPMRegs *pm, uint32_t pm_io_base);
extern const VMStateDescription vmstate_ich9_pm;

#endif /* HW_ACPI_ICH9_H */
