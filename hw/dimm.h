#ifndef QEMU_MEM_H
#define QEMU_MEM_H

#include "qemu-common.h"
#include "memory.h"
#include "sysbus.h"

#define MEMSLOT(dev) FROM_SYSBUS(DimmState, sysbus_from_qdev(dev));

typedef struct DimmState {
    SysBusDevice busdev;
    uint32_t populated; /* 1 means device has been hotplugged. Default is 0. */
    uint32_t idx; /* index in memory hotplug register/bitmap */
    uint64_t start; /* starting physical address */
    uint64_t size;
    uint32_t node; /* numa node proximity */
    MemoryRegion *mr; /* MemoryRegion for this slot. !NULL only if populated */
} DimmState;

/* mem.c */

typedef int (*dimm_hotplug_fn)(DeviceState *qdev, SysBusDevice *dev, int add);

DimmState *dimm_create(char *id, target_phys_addr_t start, uint64_t size,
        uint64_t node, uint32_t dimm_idx);
void dimm_populate(DimmState *s);
void dimm_depopulate(DimmState *s);
int dimm_do(Monitor *mon, const QDict *qdict, bool add);
DimmState *dimm_find_from_idx(uint32_t idx);
void dimm_register_hotplug(dimm_hotplug_fn hotplug, DeviceState *qdev);

#endif
