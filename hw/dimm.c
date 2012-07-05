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
#include <time.h>
#include "../exec-memory.h"
#include "qmp-commands.h"

static DeviceState *dimm_hotplug_qdev;
static dimm_hotplug_fn dimm_hotplug;
static QTAILQ_HEAD(Dimmlist, DimmState)  dimmlist;
static QTAILQ_HEAD(dimm_hp_result_head, dimm_hp_result)  dimm_hp_result_queue;

static Property dimm_properties[] = {
    DEFINE_PROP_END_OF_LIST()
};

void dimm_populate(DimmState *s)
{
    DeviceState *dev= (DeviceState*)s;
    MemoryRegion *new = NULL;

    new = g_malloc(sizeof(MemoryRegion));
    memory_region_init_ram(new, dev->id, s->size);
    vmstate_register_ram_global(new);
    memory_region_add_subregion(get_system_memory(), s->start, new);
    s->mr = new;
    s->populated = true;
}


void dimm_depopulate(DimmState *s)
{
    assert(s);
    if (s->populated) {
        vmstate_unregister_ram(s->mr, NULL);
        memory_region_del_subregion(get_system_memory(), s->mr);
        memory_region_destroy(s->mr);
        s->populated = false;
        s->mr = NULL;
    }
}

DimmState *dimm_create(char *id, uint64_t size, uint64_t node, uint32_t
        dimm_idx, bool populated)
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
    mdev->populated = populated;
    QTAILQ_INSERT_TAIL(&dimmlist, mdev, nextdimm);
    return mdev;
}

void dimm_register_hotplug(dimm_hotplug_fn hotplug, DeviceState *qdev)
{
    dimm_hotplug_qdev = qdev;
    dimm_hotplug = hotplug;
    dimm_scan_populated();
}

void dimm_activate(DimmState *slot)
{
    dimm_populate(slot);
    if (dimm_hotplug)
        dimm_hotplug(dimm_hotplug_qdev, (SysBusDevice*)slot, 1);
}

void dimm_deactivate(DimmState *slot)
{
    if (dimm_hotplug)
        dimm_hotplug(dimm_hotplug_qdev, (SysBusDevice*)slot, 0);
}

DimmState *dimm_find_from_name(char *id)
{
    Error *err = NULL;
    DeviceState *qdev;
    const char *type;
    qdev = qdev_find_recursive(sysbus_get_default(), id);
    if (qdev) {
        type = object_property_get_str(OBJECT(qdev), "type", &err);
        if (!type) {
            return NULL;
        }
        if (!strcmp(type, "dimm")) {
            return DIMM(qdev);
        }
    }    
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

    slot = dimm_find_from_name(id);

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
        dimm_deactivate(slot);
    }

    return 0;
}

DimmState *dimm_find_from_idx(uint32_t idx)
{
    DimmState *slot;

    QTAILQ_FOREACH(slot, &dimmlist, nextdimm) {
        if (slot->idx == idx) {
            return slot;
        }
    }
    return NULL;
}

/* used to calculate physical address offsets for all dimms */
void dimm_calc_offsets(dimm_calcoffset_fn calcfn)
{
    DimmState *slot;
    QTAILQ_FOREACH(slot, &dimmlist, nextdimm) {
        if (!slot->start)
            slot->start = calcfn(slot->size);
    }
}

/* used to populate and activate dimms at boot time */
void dimm_scan_populated(void)
{
    DimmState *slot;
    QTAILQ_FOREACH(slot, &dimmlist, nextdimm) {
        if (slot->populated && !slot->mr) {
            dimm_activate(slot);
        }
    }
}

void dimm_notify(uint32_t idx, uint32_t event)
{
    DimmState *s;
    s = dimm_find_from_idx(idx);
    assert(s != NULL);
    struct dimm_hp_result *result = g_malloc0(sizeof(*result));

    result->s = s;
    result->ret = event;
    switch(event) {
        case DIMM_REMOVE_SUCCESS:
            dimm_depopulate(s);
            QTAILQ_INSERT_TAIL(&dimm_hp_result_queue, result, next);
            break;
        default:
            g_free(result);
            break;
    }
}

MemHpInfoList *qmp_query_memhp(Error **errp)
{
    MemHpInfoList *head = NULL, *cur_item = NULL, *info;
    struct dimm_hp_result *item, *nextitem;

    QTAILQ_FOREACH_SAFE(item, &dimm_hp_result_queue, next, nextitem) {

        info = g_malloc0(sizeof(*info));
        info->value = g_malloc0(sizeof(*info->value));
        info->value->Dimm = g_malloc0(sizeof(char) * 32);
        info->value->request = g_malloc0(sizeof(char) * 16);
        info->value->result = g_malloc0(sizeof(char) * 16);
        switch (item->ret) {
            case DIMM_REMOVE_SUCCESS:
                strcpy(info->value->request, "hot-remove");
                strcpy(info->value->result, "success");
                break;
            case DIMM_REMOVE_FAIL:
                strcpy(info->value->request, "hot-remove");
                strcpy(info->value->result, "failure");
                break;
            case DIMM_ADD_SUCCESS:
                strcpy(info->value->request, "hot-add");
                strcpy(info->value->result, "success");
                break;
            case DIMM_ADD_FAIL:
                strcpy(info->value->request, "hot-add");
                strcpy(info->value->result, "failure");
                break;
            default:
                break;    
        }
        strcpy(info->value->Dimm, item->s->busdev.qdev.id);
        /* XXX: waiting for the qapi to support GSList */
        if (!cur_item) {
            head = cur_item = info;
        } else {
            cur_item->next = info;
            cur_item = info;
        }

        /* hotplug notification copied to qmp list, delete original item */
        QTAILQ_REMOVE(&dimm_hp_result_queue, item, next);
        g_free(item);
    }

    return head;
}
static int dimm_init(SysBusDevice *s)
{
    DimmState *slot;
    slot = DIMM(s);
    slot->mr = NULL;
    slot->populated = false;
    return 0;
}

static void dimm_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sc = SYS_BUS_DEVICE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->props = dimm_properties;
    sc->init = dimm_init;
    dimm_hotplug = NULL;
    QTAILQ_INIT(&dimmlist);
    QTAILQ_INIT(&dimm_hp_result_queue);
}

static TypeInfo dimm_info = {
    .name          = "dimm",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(DimmState),
    .class_init    = dimm_class_init,
};

static void dimm_register_types(void)
{
    type_register_static(&dimm_info);
}

type_init(dimm_register_types)
