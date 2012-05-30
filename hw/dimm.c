/*
 * Dimm device for Memory Hotplug
 *
 * Copyright ProfitBricks GmbH 2012
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

#include "trace.h"
#include "qdev.h"
#include "dimm.h"
#include "../exec-memory.h"

static DeviceState *dimm_hotplug_qdev;
static dimm_hotplug_fn dimm_hotplug;
static dimm_calcoffset_fn dimm_calcoffset;

void dimm_populate(DimmState *s)
{
    char buf[32];
    MemoryRegion *new = NULL;

    sprintf(buf, "dimm%u", s->idx);
    new = g_malloc(sizeof(MemoryRegion));
    memory_region_init_ram(new, NULL, buf, s->size);
    //vmstate_register_ram_global(new);
    memory_region_add_subregion(get_system_memory(), s->start, new);
    s->mr = new;
    s->populated = 1;
}

void dimm_depopulate(DimmState *s)
{
    assert(s);
    if (s->populated) {
        //vmstate_unregister_ram(s->mr, NULL);
        memory_region_del_subregion(get_system_memory(), s->mr);
        memory_region_destroy(s->mr);
        s->populated = 0;
        s->mr = NULL;
    }
}

DimmState *dimm_create(char *id, uint64_t size, uint64_t node, uint32_t
        dimm_idx)
{
    DeviceState *dev;
    DimmState *mdev;

    dev = sysbus_create_simple("dimm", -1, NULL);
    dev->id = id;

    mdev = DIMM(dev);
    mdev->idx = dimm_idx;
    mdev->start = 0;
    mdev->size = size;
    mdev->node = node;
    mdev->start = dimm_calcoffset(size);

    return mdev;
}

void dimm_register_hotplug(dimm_hotplug_fn hotplug, DeviceState *qdev)
{
    dimm_hotplug_qdev = qdev;
    dimm_hotplug = hotplug;
}

void dimm_register_calcoffset(dimm_calcoffset_fn calcoffset)
{
    dimm_calcoffset = calcoffset;
}

void dimm_setstart(DimmState *slot)
{
    assert(dimm_calcoffset);
    slot->start = dimm_calcoffset(slot->size);
    fprintf(stderr, "%s start address for slot %p is %lu\n", __FUNCTION__,
            slot, slot->start);
}

void dimm_activate(DimmState *slot)
{
    dimm_populate(slot);
    if (dimm_hotplug)
        dimm_hotplug(dimm_hotplug_qdev, (SysBusDevice*)slot, 1);
}

static DimmState *dimm_find(char *id)
{
    DeviceState *qdev;
    qdev = qdev_find_recursive(sysbus_get_default(), id);
    if (qdev)
        return DIMM(qdev);
    return NULL;
}

int dimm_do(Monitor *mon, const QDict *qdict, bool add)
{
    DimmState *slot = NULL;

    char *id = (char*) qdict_get_try_str(qdict, "id");
    if (!id) {
        fprintf(stderr, "ERROR %s invalid id\n",__FUNCTION__);
        return 1;
    }

    slot = dimm_find(id);

    if (!slot) {
        fprintf(stderr, "%s no slot %s found\n", __FUNCTION__, id);
        return 1;
    }

    if (add) {
        if (slot->populated) {
            fprintf(stderr, "ERROR %s slot %s already populated\n",
                    __FUNCTION__, id);
            return 1;
        }
        dimm_activate(slot);
    }
    else {
        if (!slot->populated) {
            fprintf(stderr, "ERROR %s slot %s is not populated\n",
                    __FUNCTION__, id);
            return 1;
        }
        if (dimm_hotplug)
            dimm_hotplug(dimm_hotplug_qdev, (SysBusDevice*)slot, 0);
    }
    return 0;
}

DimmState *dimm_find_from_idx(uint32_t idx)
{
    DeviceState *dev;
    DimmState *slot;
    const char *type;
    BusState *bus = sysbus_get_default();

    QTAILQ_FOREACH(dev, &bus->children, sibling) {
        type = dev->info->name;
        if (!type) {
            fprintf(stderr, "error getting device type\n");
            return NULL;
        }
        if (!strcmp(type, "dimm")) {
            slot = DIMM(dev);
            if (slot->idx == idx) {
                fprintf(stderr, "%s found slot with idx %u : %p\n",
                        __FUNCTION__, idx, slot);
                return slot;
            }
            else
                fprintf(stderr, "%s slot with idx %u !=  %u\n", __FUNCTION__,
                        slot->idx, idx);
        }
    }
    return NULL;
}

static int dimm_init(SysBusDevice *s)
{
    DimmState *slot;
    slot = DIMM(s);
    slot->mr = NULL;
    slot->populated = 0;
    return 0;
}


/*
static const VMStateDescription vmstate_apic = {
    .name = "dimm",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .load_state_old = apic_load_old,
    .fields      = (VMStateField []) {
        VMSTATE_END_OF_LIST()
    }
};*/

static SysBusDeviceInfo dimm_info = {
    .init = dimm_init,
    .qdev.name = "dimm",
    .qdev.size = sizeof(DimmState),
    .qdev.vmsd = NULL,
    .qdev.reset = NULL,
    .qdev.no_user = 1,
    .qdev.props = (Property[]) {
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void dimm_register_devices(void)
{
    sysbus_register_withprop(&dimm_info);
}

device_init(dimm_register_devices)
