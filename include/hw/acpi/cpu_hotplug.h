/*
 * QEMU ACPI hotplug utilities
 *
 * Copyright (C) 2013 Red Hat Inc
 *
 * Authors:
 *   Igor Mammedov <imammedo@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#ifndef ACPI_HOTPLUG_H
#define ACPI_HOTPLUG_H

#include "hw/acpi/acpi.h"
#include "hw/acpi/acpi_defs.h"

typedef struct AcpiCpuHotplug {
    MemoryRegion io;
    uint8_t sts[ACPI_GPE_PROC_LEN];
    uint8_t old_sts[ACPI_GPE_PROC_LEN];
} AcpiCpuHotplug;

void AcpiCpuHotplug_req(ACPIGPE *gpe, AcpiCpuHotplug *g, CPUState *cpu,
		HotplugEventType type);

void AcpiCpuHotplug_init(MemoryRegion *parent, Object *owner,
                         AcpiCpuHotplug *gpe_cpu, uint16_t base);

void AcpiCpuHotplug_eject(AcpiCpuHotplug *g, int64_t cpu_id);
#endif
