/*
 * Dimm device for Memory Hotplug
 *
 * Copyright ProfitBricks GmbH 2012
 * Copyright (C) 2014 Red Hat Inc
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

#include "hw/mem/dimm.h"
#include "qemu/config-file.h"
#include "qapi/visitor.h"
#include "qemu/range.h"

static int dimm_slot2bitmap(Object *obj, void *opaque)
{
    unsigned long *bitmap = opaque;

    if (object_dynamic_cast(obj, TYPE_DIMM)) {
        DeviceState *dev = DEVICE(obj);
        if (dev->realized) { /* count only realized DIMMs */
            DimmDevice *d = DIMM(obj);
            set_bit(d->slot, bitmap);
        }
    }

    object_child_foreach(obj, dimm_slot2bitmap, opaque);
    return 0;
}

int dimm_get_free_slot(const int *hint, int max_slots, Error **errp)
{
    unsigned long *bitmap = bitmap_new(max_slots);
    int slot = 0;

    object_child_foreach(qdev_get_machine(), dimm_slot2bitmap, bitmap);

    /* check if requested slot is not occupied */
    if (hint) {
        if (*hint >= max_slots) {
            error_setg(errp, "invalid slot# %d, should be less than %d",
                       *hint, max_slots);
        }
        if (!test_bit(*hint, bitmap)) {
            slot = *hint;
        } else {
            error_setg(errp, "slot %d is busy", *hint);
        }
        goto out;
    }

    /* search for free slot */
    slot = find_first_zero_bit(bitmap, max_slots);
    if (slot == max_slots) {
        error_setg(errp, "no free slots available");
    }
out:
    g_free(bitmap);
    return slot;
}

static gint dimm_addr_sort(gconstpointer a, gconstpointer b)
{
    DimmDevice *x = DIMM(a);
    DimmDevice *y = DIMM(b);

    return x->start - y->start;
}

static int dimm_built_list(Object *obj, void *opaque)
{
    GSList **list = opaque;

    if (object_dynamic_cast(obj, TYPE_DIMM)) {
        DeviceState *dev = DEVICE(obj);
        if (dev->realized) { /* only realized DIMMs matter */
            *list = g_slist_insert_sorted(*list, dev, dimm_addr_sort);
        }
    }

    object_child_foreach(obj, dimm_built_list, opaque);
    return 0;
}

uint64_t dimm_get_free_addr(uint64_t address_space_start,
                            uint64_t address_space_size,
                            uint64_t *hint, uint64_t size,
                            Error **errp)
{
    GSList *list = NULL, *item;
    uint64_t new_start, ret;
    uint64_t address_space_end = address_space_start + address_space_size;

    object_child_foreach(qdev_get_machine(), dimm_built_list, &list);

    if (hint) {
        new_start = *hint;
    } else {
        new_start = address_space_start;
    }

    /* find address range that will fit new DIMM */
    for (item = list; item; item = g_slist_next(item)) {
        DimmDevice *dimm = item->data;
        uint64_t dimm_size = object_property_get_int(OBJECT(dimm),
                             "size", &error_abort);
        if (ranges_overlap(dimm->start, dimm_size, new_start, size)) {
            if (hint) {
                DeviceState *d = DEVICE(dimm);
                error_setg(errp, "address range conflicts with '%s'", d->id);
                break;
            }
            new_start = dimm->start + dimm_size;
        }
    }
    ret = new_start;

    g_slist_free(list);

    if (new_start < address_space_start) {
        error_setg(errp, "can't add memory [0x%" PRIx64 ":0x%" PRIx64
                   "] at 0x%" PRIx64, new_start, size, address_space_start);
    } else if ((new_start + size) > address_space_end) {
        error_setg(errp, "can't add memory [0x%" PRIx64 ":0x%" PRIx64
                   "] beyond 0x%" PRIx64, new_start, size, address_space_end);
    }
    return ret;
}

static Property dimm_properties[] = {
    DEFINE_PROP_UINT64("start", DimmDevice, start, 0),
    DEFINE_PROP_UINT32("node", DimmDevice, node, 0),
    DEFINE_PROP_INT32("slot", DimmDevice, slot, -1),
    DEFINE_PROP_END_OF_LIST(),
};

static void dimm_get_size(Object *obj, Visitor *v, void *opaque,
                          const char *name, Error **errp)
{
    int64_t value;
    MemoryRegion *mr;
    DimmDevice *dimm = DIMM(obj);

    mr = host_memory_backend_get_memory(dimm->hostmem, errp);
    value = memory_region_size(mr);

    visit_type_int(v, &value, name, errp);
}

static void dimm_check_memdev_is_busy(Object *obj, const char *name,
                                      Object *val, Error **errp)
{
    MemoryRegion *mr;

    mr = host_memory_backend_get_memory(MEMORY_BACKEND(val), errp);
    if (memory_region_is_mapped(mr)) {
        char *path = object_get_canonical_path_component(val);
        error_setg(errp, "can't use already busy memdev: %s", path);
        g_free(path);
    } else {
        qdev_prop_allow_set_link_before_realize(obj, name, val, errp);
    }
}

static void dimm_initfn(Object *obj)
{
    DimmDevice *dimm = DIMM(obj);

    object_property_add(obj, "size", "int", dimm_get_size,
                        NULL, NULL, NULL, &error_abort);
    object_property_add_link(obj, "memdev", TYPE_MEMORY_BACKEND,
                             (Object **)&dimm->hostmem,
                             dimm_check_memdev_is_busy,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             &error_abort);
}

static void dimm_realize(DeviceState *dev, Error **errp)
{
    DimmDevice *dimm = DIMM(dev);

    if (!dimm->hostmem) {
        error_setg(errp, "'memdev' property is not set");
        return;
    }

    if (!dev->id) {
        error_setg(errp, "'id' property is not set");
        return;
    }
}

static MemoryRegion *dimm_get_memory_region(DimmDevice *dimm)
{
    return host_memory_backend_get_memory(dimm->hostmem, &error_abort);
}

static void dimm_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    DimmDeviceClass *ddc = DIMM_CLASS(oc);

    dc->realize = dimm_realize;
    dc->props = dimm_properties;

    ddc->get_memory_region = dimm_get_memory_region;
}

static TypeInfo dimm_info = {
    .name          = TYPE_DIMM,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(DimmDevice),
    .instance_init = dimm_initfn,
    .class_init    = dimm_class_init,
    .class_size    = sizeof(DimmDeviceClass),
};

static void dimm_register_types(void)
{
    type_register_static(&dimm_info);
}

type_init(dimm_register_types)
