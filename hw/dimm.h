#ifndef QEMU_MEM_H
#define QEMU_MEM_H

#include "qemu-common.h"
#include "memory.h"
#include "sysbus.h"
#include "qapi-types.h"
#include "qemu-queue.h"
#include "cpus.h"
#define MAX_DIMMS 255
#define MAX_DIMMPOOLS 8
#define DEFAULT_DIMMSIZE 1024*1024*1024

enum {
    DIMM_MIN_UNPOPULATED= 0,
    DIMM_MAX_POPULATED = 1
};

typedef enum {
    DIMM_REMOVESUCCESS_NOTIFY = 0,
    DIMM_REMOVEFAIL_NOTIFY = 1,
    DIMM_ADDSUCCESS_NOTIFY = 2,
    DIMM_ADDFAIL_NOTIFY = 3
} dimm_hp_result_code;

#define DIMM(dev) FROM_SYSBUS(DimmState, sysbus_from_qdev(dev));

typedef struct DimmState {
    SysBusDevice busdev;
    uint32_t idx; /* index in memory hotplug register/bitmap */
    uint64_t start; /* starting physical address */
    uint64_t size;
    uint32_t node; /* numa node proximity */
    MemoryRegion *mr; /* MemoryRegion for this slot. !NULL only if populated */
    bool populated; /* 1 means device has been hotplugged. Default is 0. */
    bool depopulate_pending;
} DimmState;

struct dimm_hp_result {
    DimmState *s;
    dimm_hp_result_code ret;
    QLIST_ENTRY (dimm_hp_result) next;
};

/* mem.c */

typedef int (*dimm_hotplug_fn)(DeviceState *qdev, SysBusDevice *dev, int add);
typedef target_phys_addr_t (*dimm_calcoffset_fn)(uint64_t size);

DimmState *dimm_create(char *id, uint64_t size, uint64_t node, uint32_t
        dimm_idx, bool populated);
void dimm_populate(DimmState *s);
void dimm_depopulate(DimmState *s);
int dimm_do(Monitor *mon, const QDict *qdict, bool add);
int dimm_do_range(Monitor *mon, const QDict *qdict, bool add);
DimmState *dimm_find_from_idx(uint32_t idx);
DimmState *dimm_find_from_name(char *id);
DimmState *dimm_find_next(char *pfx, uint32_t mode);
void dimm_register_hotplug(dimm_hotplug_fn hotplug, DeviceState *qdev);
void dimm_register_calcoffset(dimm_calcoffset_fn calcoffset);
void dimm_setstart(DimmState *slot);
void dimm_activate(DimmState *slot);
void dimm_deactivate(DimmState *slot);
void dimm_scan_populated(void);
int dimm_set_populated(DimmState *s);
void dimm_notify(uint32_t addr, uint32_t idx, uint32_t event);

#endif
