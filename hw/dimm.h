#ifndef QEMU_MEM_H
#define QEMU_MEM_H

#include "qemu-common.h"
#include "memory.h"
#include "sysbus.h"
#define MAX_DIMMS 256
#define MAX_DIMMPOOLS 8
#define DEFAULT_DIMMSIZE 1024*1024*1024

#define DIMM(dev) FROM_SYSBUS(DimmState, sysbus_from_qdev(dev));

typedef struct DimmState {
    SysBusDevice busdev;
    uint32_t idx; /* index in memory hotplug register/bitmap */
    uint64_t start; /* starting physical address */
    uint64_t size;
    uint32_t node; /* numa node proximity */
    MemoryRegion *mr; /* MemoryRegion for this slot. !NULL only if populated */
    bool populated; /* 1 means device has been hotplugged. Default is 0. */
} DimmState;

/* mem.c */

typedef int (*dimm_hotplug_fn)(DeviceState *qdev, SysBusDevice *dev, int add);
typedef target_phys_addr_t (*dimm_calcoffset_fn)(uint64_t size);

DimmState *dimm_create(char *id, uint64_t size, uint64_t node, uint32_t
        dimm_idx, bool populated);
void dimm_populate(DimmState *s);
void dimm_depopulate(DimmState *s);
int dimm_do(Monitor *mon, const QDict *qdict, bool add);
DimmState *dimm_find_from_idx(uint32_t idx);
DimmState *dimm_find_from_name(char *id);
void dimm_register_hotplug(dimm_hotplug_fn hotplug, DeviceState *qdev);
void dimm_register_calcoffset(dimm_calcoffset_fn calcoffset);
void dimm_setstart(DimmState *slot);
void dimm_activate(DimmState *slot);
void dimm_scan_populated(void);
void dimm_set_populated(DimmState *s);

#endif
