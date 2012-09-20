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

typedef enum {
    DIMM_NO_PENDING = 0,
    DIMM_ADD_PENDING = 1,
    DIMM_REMOVE_PENDING = 2,
} dimm_hp_pending_code;

#define TYPE_DIMM "dimm"
#define DIMM(obj) \
    OBJECT_CHECK(DimmDevice, (obj), TYPE_DIMM)
#define DIMM_CLASS(klass) \
    OBJECT_CLASS_CHECK(DimmDeviceClass, (klass), TYPE_DIMM)
#define DIMM_GET_CLASS(obj) \
    OBJECT_GET_CLASS(DimmDeviceClass, (obj), TYPE_DIMM)

typedef struct DimmDevice DimmDevice;
typedef QTAILQ_HEAD(DimmConfiglist, DimmConfig) DimmConfiglist;

typedef struct DimmDeviceClass {
    DeviceClass parent_class;

    int (*init)(DimmDevice *dev);
} DimmDeviceClass;

typedef struct DimmDevice {
    DeviceState qdev;
    uint32_t idx; /* index in memory hotplug register/bitmap */
    ram_addr_t start; /* starting physical address */
    ram_addr_t size;
    uint32_t node; /* numa node proximity */
    MemoryRegion *mr; /* MemoryRegion for this slot. !NULL only if populated */
    dimm_hp_pending_code pending; /* indicates if a hot operation is pending for this dimm */
    QTAILQ_ENTRY (DimmDevice) nextdimm;
} DimmDevice;

typedef struct DimmConfig
{
    const char *name;
    uint32_t idx; /* index in memory hotplug register/bitmap */
    ram_addr_t start; /* starting physical address */
    ram_addr_t size;
    uint32_t node; /* numa node proximity */
    uint32_t populated; /* 1 means device has been hotplugged. Default is 0. */
    QTAILQ_ENTRY (DimmConfig) nextdimmcfg;
} DimmConfig;

typedef int (*dimm_hotplug_fn)(DeviceState *qdev, DimmDevice *dev, int add);
typedef target_phys_addr_t (*dimm_calcoffset_fn)(uint64_t size);

#define TYPE_DIMM_BUS "dimmbus"
#define DIMM_BUS(obj) OBJECT_CHECK(DimmBus, (obj), TYPE_DIMM_BUS)

typedef struct DimmBus {
    BusState qbus;
    DeviceState *dimm_hotplug_qdev;
    dimm_hotplug_fn dimm_hotplug;
    dimm_hotplug_fn dimm_revert;
    dimm_calcoffset_fn dimm_calcoffset;
    DimmConfiglist dimmconfig_list;
    QTAILQ_HEAD(Dimmlist, DimmDevice) dimmlist;
    QTAILQ_HEAD(dimm_hp_result_head, dimm_hp_result)  dimm_hp_result_queue;
} DimmBus;

struct dimm_hp_result {
    const char *dimmname;
    dimm_hp_result_code ret;
    QTAILQ_ENTRY (dimm_hp_result) next;
};

void dimm_calc_offsets(dimm_calcoffset_fn calcfn);
void dimm_notify(uint32_t idx, uint32_t event);
void dimm_bus_hotplug(dimm_hotplug_fn hotplug, dimm_hotplug_fn revert, DeviceState *qdev);
void setup_fwcfg_hp_dimms(uint64_t *fw_cfg_slots);
int dimm_add(char *id);
void main_memory_bus_create(Object *parent);
void dimm_config_create(char *id, uint64_t size, uint64_t node,
        uint32_t dimm_idx, uint32_t populated);
uint64_t get_hp_memory_total(void);

#endif
