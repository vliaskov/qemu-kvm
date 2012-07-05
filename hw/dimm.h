#ifndef QEMU_DIMM_H
#define QEMU_DIMM_H

#include "qemu-common.h"
#include "memory.h"
#include "sysbus.h"
#include "qapi-types.h"
#include "qemu-queue.h"
#include "cpus.h"
#define MAX_DIMMS 255
#define DIMM_BITMAP_BYTES (MAX_DIMMS + 7) / 8
#define DEFAULT_DIMMSIZE 1024*1024*1024

typedef enum {
    DIMM_REMOVE_SUCCESS = 0,
    DIMM_REMOVE_FAIL = 1,
    DIMM_ADD_SUCCESS = 2,
    DIMM_ADD_FAIL = 3
} dimm_hp_result_code;

#define TYPE_DIMM "dimm"
#define DIMM(obj) \
    OBJECT_CHECK(DimmState, (obj), TYPE_DIMM)
#define DIMM_CLASS(klass) \
    OBJECT_CLASS_CHECK(DimmClass, (obj), TYPE_DIMM)
#define DIMM_GET_CLASS(obj) \
    OBJECT_GET_CLASS(DimmClass, (obj), TYPE_DIMM)

typedef struct DimmState {
    SysBusDevice busdev;
    uint32_t idx; /* index in memory hotplug register/bitmap */
    ram_addr_t start; /* starting physical address */
    ram_addr_t size;
    uint32_t node; /* numa node proximity */
    MemoryRegion *mr; /* MemoryRegion for this slot. !NULL only if populated */
    bool populated; /* 1 means device has been hotplugged. Default is 0. */
    QTAILQ_ENTRY (DimmState) nextdimm;
} DimmState;

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
void dimm_calc_offsets(dimm_calcoffset_fn calcfn);
void dimm_activate(DimmState *slot);
void dimm_deactivate(DimmState *slot);
void dimm_scan_populated(void);
void dimm_notify(uint32_t idx, uint32_t event);


#endif
