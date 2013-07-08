/* Support for generating ACPI tables and passing them to Guests
 *
 * Copyright (C) 2008-2010  Kevin O'Connor <kevin@koconnor.net>
 * Copyright (C) 2006 Fabrice Bellard
 * Copyright (C) 2013 Red Hat Inc
 *
 * Author: Michael S. Tsirkin <mst@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/i386/acpi-build.h"
#include <stddef.h>
#include <glib.h>
#include "qemu/bitmap.h"
#include "qemu/range.h"
#include "hw/pci/pci.h"
#include "qom/cpu.h"
#include "hw/i386/pc.h"
#include "target-i386/cpu.h"
#include "hw/timer/hpet.h"
#include "hw/i386/acpi-defs.h"
#include "hw/acpi/acpi.h"
#include "hw/nvram/fw_cfg.h"
#include "hw/i386/bios-linker-loader.h"
#include "hw/loader.h"

#define ACPI_BUILD_APPNAME  "Bochs"
#define ACPI_BUILD_APPNAME6 "BOCHS "
#define ACPI_BUILD_APPNAME4 "BXPC"

#define ACPI_BUILD_DPRINTF(level, fmt, ...) do {} while(0)

#define ACPI_BUILD_TABLE_FILE "etc/acpi/tables"
#define ACPI_BUILD_RSDP_FILE "etc/acpi/rsdp"

static void
build_header(GArray *linker, GArray *table_data,
             AcpiTableHeader *h, uint32_t sig, int len, uint8_t rev)
{
    h->signature = cpu_to_le32(sig);
    h->length = cpu_to_le32(len);
    h->revision = rev;
    memcpy(h->oem_id, ACPI_BUILD_APPNAME6, 6);
    memcpy(h->oem_table_id, ACPI_BUILD_APPNAME4, 4);
    memcpy(h->oem_table_id + 4, (void*)&sig, 4);
    h->oem_revision = cpu_to_le32(1);
    memcpy(h->asl_compiler_id, ACPI_BUILD_APPNAME4, 4);
    h->asl_compiler_revision = cpu_to_le32(1);
    h->checksum = 0;
    /* Checksum to be filled in by Guest linker */
    bios_linker_add_checksum(linker, ACPI_BUILD_TABLE_FILE,
                             table_data->data, h, len, &h->checksum);
}

#define ACPI_PORT_SMI_CMD           0x00b2 /* TODO: this is APM_CNT_IOPORT */
#define ACPI_PORT_PM_BASE      0xb000

static inline void *acpi_data_push(GArray *table_data, unsigned size)
{
    unsigned off = table_data->len;
    g_array_set_size(table_data, off + size);
    return table_data->data + off;
}

static unsigned acpi_data_len(GArray *table)
{
    return table->len * g_array_get_element_size(table);
}

static inline void acpi_add_table(GArray *table_offsets, GArray *table_data)
{
    uint32_t offset = cpu_to_le32(table_data->len);
    g_array_append_val(table_offsets, offset);
}

/* FACS */
static void
build_facs(GArray *table_data, GArray *linker, PcGuestInfo *guest_info)
{
    AcpiFacsDescriptorRev1 *facs = acpi_data_push(table_data, sizeof *facs);
    facs->signature = cpu_to_le32(ACPI_FACS_SIGNATURE);
    facs->length = cpu_to_le32(sizeof(*facs));
}

/* Load chipset information into FADT */
static void fadt_setup(AcpiFadtDescriptorRev1 *fadt, PcGuestInfo *guest_info)
{
    fadt->model = 1;
    fadt->reserved1 = 0;
    fadt->sci_int = cpu_to_le16(guest_info->sci_int);
    fadt->smi_cmd = cpu_to_le32(ACPI_PORT_SMI_CMD);
    fadt->acpi_enable = guest_info->acpi_enable_cmd;
    fadt->acpi_disable = guest_info->acpi_disable_cmd;
    fadt->pm1a_evt_blk = cpu_to_le32(ACPI_PORT_PM_BASE);
    fadt->pm1a_cnt_blk = cpu_to_le32(ACPI_PORT_PM_BASE + 0x04);
    fadt->pm_tmr_blk = cpu_to_le32(ACPI_PORT_PM_BASE + 0x08);
    fadt->gpe0_blk = cpu_to_le32(guest_info->gpe0_blk);
    fadt->pm1_evt_len = 4;
    fadt->pm1_cnt_len = 2;
    fadt->pm_tmr_len = 4;
    fadt->gpe0_blk_len = guest_info->gpe0_blk_len;
    fadt->plvl2_lat = cpu_to_le16(0xfff); /* C2 state not supported */
    fadt->plvl3_lat = cpu_to_le16(0xfff); /* C3 state not supported */
    fadt->flags = cpu_to_le32((1 << ACPI_FADT_F_WBINVD) |
                              (1 << ACPI_FADT_F_PROC_C1) |
                              (1 << ACPI_FADT_F_SLP_BUTTON) |
                              (1 << ACPI_FADT_F_RTC_S4));
    if (guest_info->fix_rtc) {
        fadt->flags |= cpu_to_le32(1 << ACPI_FADT_F_FIX_RTC);
    }
    if (guest_info->platform_timer) {
        fadt->flags |= cpu_to_le32(1 << ACPI_FADT_F_USE_PLATFORM_CLOCK);
    }
}


/* FADT */
static void
build_fadt(GArray *table_data, GArray *linker, PcGuestInfo *guest_info,
           unsigned facs, unsigned dsdt)
{
    AcpiFadtDescriptorRev1 *fadt = acpi_data_push(table_data, sizeof(*fadt));

    fadt->firmware_ctrl = cpu_to_le32(facs);
    /* FACS address to be filled by Guest linker */
    bios_linker_add_pointer(linker, ACPI_BUILD_TABLE_FILE, ACPI_BUILD_TABLE_FILE,
                            table_data, &fadt->firmware_ctrl,
                            sizeof fadt->firmware_ctrl);

    fadt->dsdt = cpu_to_le32(dsdt);
    /* DSDT address to be filled by Guest linker */
    bios_linker_add_pointer(linker, ACPI_BUILD_TABLE_FILE, ACPI_BUILD_TABLE_FILE,
                            table_data, &fadt->dsdt,
                            sizeof fadt->dsdt);
    
    fadt_setup(fadt, guest_info);

    build_header(linker, table_data,
                 (void*)fadt, ACPI_FACP_SIGNATURE, sizeof(*fadt), 1);
}

static void
build_madt(GArray *table_data, GArray *linker, FWCfgState *fw_cfg, PcGuestInfo *guest_info)
{
    int madt_size;

    AcpiMultipleApicTable *madt;
    AcpiMadtProcessorApic *apic;
    AcpiMadtIoApic *io_apic;
    AcpiMadtIntsrcovr *intsrcovr;
    AcpiMadtLocalNmi *local_nmi;

    madt_size = (sizeof(AcpiMultipleApicTable)
                 + sizeof(AcpiMadtProcessorApic) * guest_info->apic_id_limit
                 + sizeof(AcpiMadtIoApic)
                 + sizeof(AcpiMadtIntsrcovr) * 16
                 + sizeof(AcpiMadtLocalNmi));
    madt = acpi_data_push(table_data, madt_size);
    madt->local_apic_address = cpu_to_le32(APIC_DEFAULT_ADDRESS);
    madt->flags = cpu_to_le32(1);
    apic = (void*)&madt[1];
    int i;
    for (i=0; i < guest_info->apic_id_limit; i++) {
        apic->type = ACPI_APIC_PROCESSOR;
        apic->length = sizeof(*apic);
        apic->processor_id = i;
        apic->local_apic_id = i;
        if (test_bit(i, guest_info->found_cpus))
            apic->flags = cpu_to_le32(1);
        else
            apic->flags = cpu_to_le32(0);
        apic++;
    }
    io_apic = (void*)apic;
    io_apic->type = ACPI_APIC_IO;
    io_apic->length = sizeof(*io_apic);
#define ACPI_BUILD_IOAPIC_ID 0x0
    io_apic->io_apic_id = ACPI_BUILD_IOAPIC_ID;
    io_apic->address = cpu_to_le32(IO_APIC_DEFAULT_ADDRESS);
    io_apic->interrupt = cpu_to_le32(0);

    intsrcovr = (void*)&io_apic[1];
    if (guest_info->apic_xrupt_override) {
        memset(intsrcovr, 0, sizeof(*intsrcovr));
        intsrcovr->type   = ACPI_APIC_XRUPT_OVERRIDE;
        intsrcovr->length = sizeof(*intsrcovr);
        intsrcovr->source = 0;
        intsrcovr->gsi    = cpu_to_le32(2);
        intsrcovr->flags  = cpu_to_le16(0); /* conforms to bus specifications */
        intsrcovr++;
    }
    for (i = 1; i < 16; i++) {
#define ACPI_BUILD_PCI_IRQS ((1<<5) | (1<<9) | (1<<10) | (1<<11))
        if (!(ACPI_BUILD_PCI_IRQS & (1 << i)))
            /* No need for a INT source override structure. */
            continue;
        memset(intsrcovr, 0, sizeof(*intsrcovr));
        intsrcovr->type   = ACPI_APIC_XRUPT_OVERRIDE;
        intsrcovr->length = sizeof(*intsrcovr);
        intsrcovr->source = i;
        intsrcovr->gsi    = cpu_to_le32(i);
        intsrcovr->flags  = cpu_to_le16(0xd); /* active high, level triggered */
        intsrcovr++;
    }

    local_nmi = (void*)intsrcovr;
    local_nmi->type         = ACPI_APIC_LOCAL_NMI;
    local_nmi->length       = sizeof(*local_nmi);
    local_nmi->processor_id = 0xff; /* all processors */
    local_nmi->flags        = cpu_to_le16(0);
    local_nmi->lint         = 1; /* ACPI_LINT1 */
    local_nmi++;

    build_header(linker, table_data,
                 (void*)madt, ACPI_APIC_SIGNATURE,
                 (void*)local_nmi - (void*)madt, 1);
}

/* Encode a hex value */
static inline char acpi_get_hex(uint32_t val) {
    val &= 0x0f;
    return (val <= 9) ? ('0' + val) : ('A' + val - 10);
}

/* Encode a length in an SSDT. */
static uint8_t *
acpi_encode_len(uint8_t *ssdt_ptr, int length, int bytes)
{
    switch (bytes) {
    default:
    case 4: ssdt_ptr[3] = ((length >> 20) & 0xff);
    case 3: ssdt_ptr[2] = ((length >> 12) & 0xff);
    case 2: ssdt_ptr[1] = ((length >> 4) & 0xff);
            ssdt_ptr[0] = (((bytes - 1) & 0x3) << 6) | (length & 0x0f);
            break;
    case 1: ssdt_ptr[0] = length & 0x3f;
    }
    return ssdt_ptr + bytes;
}

#include "hw/i386/ssdt-proc.hex"

/* 0x5B 0x83 ProcessorOp PkgLength NameString ProcID */
#define ACPI_PROC_OFFSET_CPUHEX (*ssdt_proc_name - *ssdt_proc_start + 2)
#define ACPI_PROC_OFFSET_CPUID1 (*ssdt_proc_name - *ssdt_proc_start + 4)
#define ACPI_PROC_OFFSET_CPUID2 (*ssdt_proc_id - *ssdt_proc_start)
#define ACPI_PROC_SIZEOF (*ssdt_proc_end - *ssdt_proc_start)
#define ACPI_PROC_AML (ssdp_proc_aml + *ssdt_proc_start)

/* 0x5B 0x82 DeviceOp PkgLength NameString */
#define ACPI_PCIHP_OFFSET_HEX (*ssdt_pcihp_name - *ssdt_pcihp_start + 1)
#define ACPI_PCIHP_OFFSET_ID (*ssdt_pcihp_id - *ssdt_pcihp_start)
#define ACPI_PCIHP_OFFSET_ADR (*ssdt_pcihp_adr - *ssdt_pcihp_start)
#define ACPI_PCIHP_OFFSET_EJ0 (*ssdt_pcihp_ej0 - *ssdt_pcihp_start)
#define ACPI_PCIHP_SIZEOF (*ssdt_pcihp_end - *ssdt_pcihp_start)
#define ACPI_PCIHP_AML (ssdp_pcihp_aml + *ssdt_pcihp_start)

#define ACPI_SSDT_SIGNATURE 0x54445353 /* SSDT */
#define ACPI_SSDT_HEADER_LENGTH 36

#include "hw/i386/ssdt-misc.hex"
#include "hw/i386/ssdt-pcihp.hex"

static uint8_t*
build_notify(uint8_t *ssdt_ptr, const char *name, int skip, int count,
             const char *target, int ofs)
{
    int i;

    count -= skip;

    *(ssdt_ptr++) = 0x14; /* MethodOp */
    ssdt_ptr = acpi_encode_len(ssdt_ptr, 2+5+(12*count), 2);
    memcpy(ssdt_ptr, name, 4);
    ssdt_ptr += 4;
    *(ssdt_ptr++) = 0x02; /* MethodOp */

    for (i = skip; count-- > 0; i++) {
        *(ssdt_ptr++) = 0xA0; /* IfOp */
        ssdt_ptr = acpi_encode_len(ssdt_ptr, 11, 1);
        *(ssdt_ptr++) = 0x93; /* LEqualOp */
        *(ssdt_ptr++) = 0x68; /* Arg0Op */
        *(ssdt_ptr++) = 0x0A; /* BytePrefix */
        *(ssdt_ptr++) = i;
        *(ssdt_ptr++) = 0x86; /* NotifyOp */
        memcpy(ssdt_ptr, target, 4);
        ssdt_ptr[ofs] = acpi_get_hex(i >> 4);
        ssdt_ptr[ofs + 1] = acpi_get_hex(i);
        ssdt_ptr += 4;
        *(ssdt_ptr++) = 0x69; /* Arg1Op */
    }
    return ssdt_ptr;
}

static void patch_pcihp(int slot, uint8_t *ssdt_ptr, uint32_t eject)
{
    ssdt_ptr[ACPI_PCIHP_OFFSET_HEX] = acpi_get_hex(slot >> 4);
    ssdt_ptr[ACPI_PCIHP_OFFSET_HEX+1] = acpi_get_hex(slot);
    ssdt_ptr[ACPI_PCIHP_OFFSET_ID] = slot;
    ssdt_ptr[ACPI_PCIHP_OFFSET_ADR + 2] = slot;

    /* Runtime patching of ACPI_EJ0: to disable hotplug for a slot,
     * replace the method name: _EJ0 by ACPI_EJ0_. */
    /* Sanity check */
    assert (!memcmp(ssdt_ptr + ACPI_PCIHP_OFFSET_EJ0, "_EJ0", 4));

    if (!eject) {
        memcpy(ssdt_ptr + ACPI_PCIHP_OFFSET_EJ0, "EJ0_", 4);
    }
}

static void
build_ssdt(GArray *table_data, GArray *linker,
           FWCfgState *fw_cfg, PcGuestInfo *guest_info)
{
    int acpi_cpus = MIN(0xff, guest_info->apic_id_limit);
    int length = (sizeof(ssdp_misc_aml)                     /* _S3_ / _S4_ / _S5_ */
                  + (1+3+4)                                 /* Scope(_SB_) */
                  + (acpi_cpus * ACPI_PROC_SIZEOF)               /* procs */
                  + (1+2+5+(12*acpi_cpus))                  /* NTFY */
                  + (6+2+1+(1*acpi_cpus))                   /* CPON */
                  + (1+3+4)                                 /* Scope(PCI0) */
                  + ((PCI_SLOT_MAX - 1) * ACPI_PCIHP_SIZEOF)        /* slots */
                  + (1+2+5+(12*(PCI_SLOT_MAX - 1))));          /* PCNT */
    uint8_t *ssdt = acpi_data_push(table_data, length);
    uint8_t *ssdt_ptr = ssdt;

    /* Copy header and encode fwcfg values in the S3_ / S4_ / S5_ packages */
    memcpy(ssdt_ptr, ssdp_misc_aml, sizeof(ssdp_misc_aml));
    if (guest_info->s3_disabled) {
        ssdt_ptr[acpi_s3_name[0]] = 'X';
    }
    if (guest_info->s4_disabled) {
        ssdt_ptr[acpi_s4_name[0]] = 'X';
    } else {
        ssdt_ptr[acpi_s4_pkg[0] + 1] = ssdt[acpi_s4_pkg[0] + 3] =
            guest_info->s4_val;
    }

    *(uint32_t*)&ssdt_ptr[acpi_pci32_start[0]] =
        cpu_to_le32(guest_info->pci_info.w32.begin);
    *(uint32_t*)&ssdt_ptr[acpi_pci32_end[0]] =
        cpu_to_le32(guest_info->pci_info.w32.end - 1);

    if (guest_info->pci_info.w64.end > guest_info->pci_info.w64.begin) {
        ssdt_ptr[acpi_pci64_valid[0]] = 1;
        *(uint64_t*)&ssdt_ptr[acpi_pci64_start[0]] =
            cpu_to_le64(guest_info->pci_info.w64.begin);
        *(uint64_t*)&ssdt_ptr[acpi_pci64_end[0]] =
            cpu_to_le64(guest_info->pci_info.w64.end - 1);
        *(uint64_t*)&ssdt_ptr[acpi_pci64_length[0]] =
            cpu_to_le64(guest_info->pci_info.w64.end -
                        guest_info->pci_info.w64.begin);
    } else {
        ssdt_ptr[acpi_pci64_valid[0]] = 0;
    }

    *(uint16_t *)(ssdt_ptr + *ssdt_isa_pest) =
        cpu_to_le16(guest_info->pvpanic_port);

    ssdt_ptr += sizeof(ssdp_misc_aml);

    /* build Scope(_SB_) header */
    *(ssdt_ptr++) = 0x10; /* ScopeOp */
    ssdt_ptr = acpi_encode_len(ssdt_ptr, length - (ssdt_ptr - ssdt), 3);
    *(ssdt_ptr++) = '_';
    *(ssdt_ptr++) = 'S';
    *(ssdt_ptr++) = 'B';
    *(ssdt_ptr++) = '_';

    /* build Processor object for each processor */
    int i;
    for (i=0; i<acpi_cpus; i++) {
        memcpy(ssdt_ptr, ACPI_PROC_AML, ACPI_PROC_SIZEOF);
        ssdt_ptr[ACPI_PROC_OFFSET_CPUHEX] = acpi_get_hex(i >> 4);
        ssdt_ptr[ACPI_PROC_OFFSET_CPUHEX+1] = acpi_get_hex(i);
        ssdt_ptr[ACPI_PROC_OFFSET_CPUID1] = i;
        ssdt_ptr[ACPI_PROC_OFFSET_CPUID2] = i;
        ssdt_ptr += ACPI_PROC_SIZEOF;
    }

    /* build "Method(NTFY, 2) {If (LEqual(Arg0, 0x00)) {Notify(CP00, Arg1)} ...}" */
    /* Arg0 = Processor ID = APIC ID */
    ssdt_ptr = build_notify(ssdt_ptr, "NTFY", 0, acpi_cpus, "CP00", 2);

    /* build "Name(CPON, Package() { One, One, ..., Zero, Zero, ... })" */
    *(ssdt_ptr++) = 0x08; /* NameOp */
    *(ssdt_ptr++) = 'C';
    *(ssdt_ptr++) = 'P';
    *(ssdt_ptr++) = 'O';
    *(ssdt_ptr++) = 'N';
    *(ssdt_ptr++) = 0x12; /* PackageOp */
    ssdt_ptr = acpi_encode_len(ssdt_ptr, 2+1+(1*acpi_cpus), 2);
    *(ssdt_ptr++) = acpi_cpus;
    for (i=0; i<acpi_cpus; i++)
        *(ssdt_ptr++) = (test_bit(i, guest_info->found_cpus)) ? 0x01 : 0x00;

    /* build Scope(PCI0) opcode */
    *(ssdt_ptr++) = 0x10; /* ScopeOp */
    ssdt_ptr = acpi_encode_len(ssdt_ptr, length - (ssdt_ptr - ssdt), 3);
    *(ssdt_ptr++) = 'P';
    *(ssdt_ptr++) = 'C';
    *(ssdt_ptr++) = 'I';
    *(ssdt_ptr++) = '0';

    /* build Device object for each slot */
    for (i = 1; i < PCI_SLOT_MAX; i++) {
        bool eject = test_bit(i, guest_info->slot_hotplug_enable);
        memcpy(ssdt_ptr, ACPI_PCIHP_AML, ACPI_PCIHP_SIZEOF);
        patch_pcihp(i, ssdt_ptr, eject);
        ssdt_ptr += ACPI_PCIHP_SIZEOF;
    }

    ssdt_ptr = build_notify(ssdt_ptr, "PCNT", 1, PCI_SLOT_MAX, "S00_", 1);

    build_header(linker, table_data,
                 (void*)ssdt, ACPI_SSDT_SIGNATURE, ssdt_ptr - ssdt, 1);
}

static void
build_hpet(GArray *table_data, GArray *linker)
{
    Acpi20Hpet *hpet;

    hpet = acpi_data_push(table_data, sizeof(*hpet));
    /* Note timer_block_id value must be kept in sync with value advertised by
     * emulated hpet
     */
    hpet->timer_block_id = cpu_to_le32(0x8086a201);
    hpet->addr.address = cpu_to_le64(HPET_BASE);
    build_header(linker, table_data,
                 (void*)hpet, ACPI_HPET_SIGNATURE, sizeof(*hpet), 1);
}

static void
acpi_build_srat_memory(AcpiSratMemoryAffinity *numamem,
                       uint64_t base, uint64_t len, int node, int enabled)
{
    numamem->type = ACPI_SRAT_MEMORY;
    numamem->length = sizeof(*numamem);
    memset(numamem->proximity, 0, 4);
    numamem->proximity[0] = node;
    numamem->flags = cpu_to_le32(!!enabled);
    numamem->base_addr = cpu_to_le64(base);
    numamem->range_length = cpu_to_le64(len);
}

static void
build_srat(GArray *table_data, GArray *linker,
           FWCfgState *fw_cfg, PcGuestInfo *guest_info)
{
    AcpiSystemResourceAffinityTable *srat;
    AcpiSratProcessorAffinity *core;
    AcpiSratMemoryAffinity *numamem;

    int i;
    uint64_t curnode;
    int srat_size;
    int slots;
    uint64_t mem_len, mem_base, next_base;

    srat_size = sizeof(*srat) +
        sizeof(AcpiSratProcessorAffinity) * guest_info->apic_id_limit +
        sizeof(AcpiSratMemoryAffinity) * (guest_info->numa_nodes + 2);

    srat = acpi_data_push(table_data, srat_size);
    srat->reserved1 = cpu_to_le32(1);
    core = (void*)(srat + 1);

    for (i = 0; i < guest_info->apic_id_limit; ++i) {
        core->type = ACPI_SRAT_PROCESSOR;
        core->length = sizeof(*core);
        core->local_apic_id = i;
        curnode = guest_info->node_cpu[i];
        core->proximity_lo = curnode;
        memset(core->proximity_hi, 0, 3);
        core->local_sapic_eid = 0;
        if (test_bit(i, guest_info->found_cpus))
            core->flags = cpu_to_le32(1);
        else
            core->flags = cpu_to_le32(0);
        core++;
    }


    /* the memory map is a bit tricky, it contains at least one hole
     * from 640k-1M and possibly another one from 3.5G-4G.
     */
    numamem = (void*)core;
    slots = 0;
    next_base = 0;

    acpi_build_srat_memory(numamem, 0, 640*1024, 0, 1);
    next_base = 1024 * 1024;
    numamem++;
    slots++;
    for (i = 1; i < guest_info->numa_nodes + 1; ++i) {
        mem_base = next_base;
        mem_len = guest_info->node_mem[i - 1];
        if (i == 1)
            mem_len -= 1024 * 1024;
        next_base = mem_base + mem_len;

        /* Cut out the ACPI_PCI hole */
        if (mem_base <= guest_info->ram_size &&
            next_base > guest_info->ram_size) {
            mem_len -= next_base - guest_info->ram_size;
            if (mem_len > 0) {
                acpi_build_srat_memory(numamem, mem_base, mem_len, i-1, 1);
                numamem++;
                slots++;
            }
            mem_base = 1ULL << 32;
            mem_len = next_base - guest_info->ram_size;
            next_base += (1ULL << 32) - guest_info->ram_size;
        }
        acpi_build_srat_memory(numamem, mem_base, mem_len, i-1, 1);
        numamem++;
        slots++;
    }
    for (; slots < guest_info->numa_nodes + 2; slots++) {
        acpi_build_srat_memory(numamem, 0, 0, 0, 0);
        numamem++;
    }

    build_header(linker, table_data,
                 (void*)srat, ACPI_SRAT_SIGNATURE, srat_size, 1);
}

static void
build_mcfg_q35(GArray *table_data, GArray *linker, PcGuestInfo *guest_info)
{
    AcpiTableMcfg *mcfg;

    int len = sizeof(*mcfg) + 1 * sizeof(mcfg->allocation[0]);
    mcfg = acpi_data_push(table_data, len);
    mcfg->allocation[0].address = cpu_to_le64(guest_info->mcfg_base);
    /* Only a single allocation so no need to play with segments */
    mcfg->allocation[0].pci_segment = cpu_to_le16(0);
    mcfg->allocation[0].start_bus_number = 0;
    mcfg->allocation[0].end_bus_number = 0xFF;

    build_header(linker, table_data, (void *)mcfg, ACPI_MCFG_SIGNATURE, len, 1);
}

static void
build_dsdt(GArray *table_data, GArray *linker, PcGuestInfo *guest_info)
{
    void *dsdt;
    assert(guest_info->dsdt_code && guest_info->dsdt_size);
    dsdt = acpi_data_push(table_data, guest_info->dsdt_size);
    memcpy(dsdt, guest_info->dsdt_code, guest_info->dsdt_size);
}

/* Build final rsdt table */
static void
build_rsdt(GArray *table_data, GArray *linker, GArray *table_offsets)
{
    AcpiRsdtDescriptorRev1 *rsdt;
    size_t rsdt_len;
    int i;

    rsdt_len = sizeof(*rsdt) + sizeof(uint32_t) * table_offsets->len;
    rsdt = acpi_data_push(table_data, rsdt_len);
    memcpy(rsdt->table_offset_entry, table_offsets->data,
           sizeof(uint32_t) * table_offsets->len);
    for (i = 0; i < table_offsets->len; ++i) {
        /* rsdt->table_offset_entry to be filled by Guest linker */
        bios_linker_add_pointer(linker,
                                ACPI_BUILD_TABLE_FILE, ACPI_BUILD_TABLE_FILE,
                                table_data, &rsdt->table_offset_entry[i],
                                sizeof(uint32_t));
    }
    build_header(linker, table_data,
                 (void*)rsdt, ACPI_RSDT_SIGNATURE, rsdt_len, 1);
}

static GArray *
build_rsdp(GArray *linker, unsigned rsdt)
{
    GArray *rsdp_table;
    AcpiRsdpDescriptor *rsdp;

    rsdp_table = g_array_new(false, true /* clear */, sizeof *rsdp);
    g_array_set_size(rsdp_table, 1);
    rsdp = (void *)rsdp_table->data;

    bios_linker_alloc(linker, ACPI_BUILD_RSDP_FILE, 1, true /* fseg memory */);

    rsdp->signature = cpu_to_le64(ACPI_RSDP_SIGNATURE);
    memcpy(rsdp->oem_id, ACPI_BUILD_APPNAME6, 6);
    rsdp->rsdt_physical_address = cpu_to_le32(rsdt);
    /* Address to be filled by Guest linker */
    bios_linker_add_pointer(linker, ACPI_BUILD_RSDP_FILE, ACPI_BUILD_TABLE_FILE,
                            rsdp_table, &rsdp->rsdt_physical_address,
                            sizeof rsdp->rsdt_physical_address);
    rsdp->checksum = 0;
    /* Checksum to be filled by Guest linker */
    bios_linker_add_checksum(linker, ACPI_BUILD_RSDP_FILE,
                             rsdp, rsdp, sizeof *rsdp, &rsdp->checksum);

    return rsdp_table;
}

static void acpi_add_rom_blob(PcGuestInfo *guest_info, GArray *blob,
                              const char *name, unsigned align)
{
    MemoryRegion *mr = g_malloc(sizeof(*mr));

    /* Align size to multiple of given size. This reduces the chance
     * we need to change size in the future (breaking cross version migration).
     */
    g_array_set_size(blob, (ROUND_UP(acpi_data_len(blob), align) +
                            g_array_get_element_size(blob) - 1) /
                             g_array_get_element_size(blob));
    memory_region_init_ram_ptr(mr, name,
                               acpi_data_len(blob), blob->data);
    memory_region_set_readonly(mr, true);
    vmstate_register_ram_global(mr);
    rom_add_blob(ACPI_BUILD_TABLE_FILE, blob->data, acpi_data_len(blob),
                 -1, mr);

    fw_cfg_add_file(guest_info->fw_cfg, name,
                    blob->data, acpi_data_len(blob));
}

#define ACPI_MAX_ACPI_TABLES 20
void acpi_setup(PcGuestInfo *guest_info)
{
    GArray *table_data, *table_offsets, *rsdp, *linker;
    unsigned facs, dsdt, rsdt;

    if (!guest_info->fw_cfg) {
        ACPI_BUILD_DPRINTF(3, "No fw cfg. Boiling out.\n");
        return;
    }

    if (!guest_info->has_acpi_build) {
        ACPI_BUILD_DPRINTF(3, "ACPI build disabled. Boiling out.\n");
        return;
    }

    table_data = g_array_new(false, true /* clear */, 1);
    table_offsets = g_array_new(false, true /* clear */,
                                        sizeof(uint32_t));
    linker = bios_linker_init();

    ACPI_BUILD_DPRINTF(3, "init ACPI tables\n");

    bios_linker_alloc(linker, ACPI_BUILD_TABLE_FILE, 64 /* Ensure FACS is aligned */,
                      false /* high memory */);

    /*
     * FACS is pointed to by FADT.
     * We place it first since it's the only table that has alignment
     * requirements.
     */
    facs = table_data->len;
    build_facs(table_data, linker, guest_info);

    /* DSDT is pointed to by FADT */
    dsdt = table_data->len;
    build_dsdt(table_data, linker, guest_info);

    /* ACPI tables pointed to by RSDT */
    acpi_add_table(table_offsets, table_data);
    build_fadt(table_data, linker, guest_info, facs, dsdt);
    acpi_add_table(table_offsets, table_data);
    build_ssdt(table_data, linker, guest_info->fw_cfg, guest_info);
    acpi_add_table(table_offsets, table_data);
    build_madt(table_data, linker, guest_info->fw_cfg, guest_info);
    acpi_add_table(table_offsets, table_data);
    if (guest_info->has_hpet) {
        build_hpet(table_data, linker);
    }
    if (guest_info->numa_nodes) {
        acpi_add_table(table_offsets, table_data);
        build_srat(table_data, linker, guest_info->fw_cfg, guest_info);
    }
    if (guest_info->mcfg_base) {
        acpi_add_table(table_offsets, table_data);
        build_mcfg_q35(table_data, linker, guest_info);
    }

    /* RSDT is pointed to by RSDP */
    rsdt = table_data->len;
    build_rsdt(table_data, linker, table_offsets);

    /* RSDP is in FSEG memory, so allocate it separately */
    rsdp = build_rsdp(linker, rsdt);

    /* Now expose it all to Guest */
    acpi_add_rom_blob(guest_info, table_data,
                      ACPI_BUILD_TABLE_FILE, 0x10000);

    acpi_add_rom_blob(guest_info, linker,
                      "etc/linker-script", TARGET_PAGE_SIZE);

    /*
     * RSDP is small so it's easy to keep it immutable, no need to
     * bother with ROM blobs.
     */
    fw_cfg_add_file(guest_info->fw_cfg, ACPI_BUILD_RSDP_FILE,
                    rsdp->data, acpi_data_len(rsdp));

    /* Cleanup GArray wrappers and memory if no longer used. */
    bios_linker_cleanup(linker);
    g_array_free(table_offsets, true);
    g_array_free(rsdp, false);
    g_array_free(table_data, false);
}
