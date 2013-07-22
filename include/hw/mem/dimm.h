/*
 * DIMM device
 *
 * Copyright ProfitBricks GmbH 2012
 * Copyright (C) 2013 Red Hat Inc
 *
 * Authors:
 *  Vasilis Liaskovitis <vasilis.liaskovitis@profitbricks.com>
 *  Igor Mammedov <imammedo@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_DIMM_H
#define QEMU_DIMM_H

#include "exec/memory.h"
#include "sysemu/hostmem.h"
#include "hw/qdev.h"

#define DEFAULT_DIMMSIZE (1024*1024*1024)

#define TYPE_DIMM "dimm"
#define DIMM(obj) \
    OBJECT_CHECK(DimmDevice, (obj), TYPE_DIMM)
#define DIMM_CLASS(oc) \
    OBJECT_CLASS_CHECK(DimmDeviceClass, (oc), TYPE_DIMM)
#define DIMM_GET_CLASS(obj) \
    OBJECT_GET_CLASS(DimmDeviceClass, (obj), TYPE_DIMM)

/**
 * DimmDevice:
 * @start: starting guest physical address, where @DimmDevice is mapped.
 *         Default value: 0, means that address is auto-allocated.
 * @node: numa node to which @DimmDevice is attached.
 * @slot: slot number into which @DimmDevice is plugged in.
 *        Default value: -1, means that slot is auto-allocated.
 * @hostmem: host memory backend providing memory for @DimmDevice
 */
typedef struct DimmDevice {
    /* private */
    DeviceState parent_obj;

    /* public */
    ram_addr_t start;
    uint32_t node;
    int32_t slot;
    HostMemoryBackend *hostmem;
} DimmDevice;

/**
 * DimmDeviceClass:
 * @get_memory_region: returns #MemoryRegion associated with @dimm
 */
typedef struct DimmDeviceClass {
    /* private */
    DeviceClass parent_class;

    /* public */
    MemoryRegion *(*get_memory_region)(DimmDevice *dimm);
} DimmDeviceClass;


uint64_t dimm_get_free_addr(uint64_t address_space_start,
                            uint64_t address_space_size,
                            uint64_t *hint, uint64_t size,
                            Error **errp);

int dimm_get_free_slot(const int *hint, int max_slots, Error **errp);
#endif
