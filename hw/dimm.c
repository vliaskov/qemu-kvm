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

/* the system-wide memory bus. */
static DimmBus *main_memory_bus;
/* the following list is used to hold dimm config info before machine
 * initialization. After machine init, the list is emptied and not used anymore.*/
static DimmConfiglist dimmconfig_list = QTAILQ_HEAD_INITIALIZER(dimmconfig_list);
extern ram_addr_t ram_size;

static void dimmbus_dev_print(Monitor *mon, DeviceState *dev, int indent);
static char *dimmbus_get_fw_dev_path(DeviceState *dev);

static Property dimm_properties[] = {
    DEFINE_PROP_UINT64("start", DimmDevice, start, 0),
    DEFINE_PROP_UINT64("size", DimmDevice, size, DEFAULT_DIMMSIZE),
    DEFINE_PROP_UINT32("node", DimmDevice, node, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void dimmbus_dev_print(Monitor *mon, DeviceState *dev, int indent)
{
}

static char *dimmbus_get_fw_dev_path(DeviceState *dev)
{
    char path[40];

    snprintf(path, sizeof(path), "%s", qdev_fw_name(dev));
    return strdup(path);
}

static void dimm_bus_class_init(ObjectClass *klass, void *data)
{
    BusClass *k = BUS_CLASS(klass);

    k->print_dev = dimmbus_dev_print;
    k->get_fw_dev_path = dimmbus_get_fw_dev_path;
}

static void dimm_bus_initfn(Object *obj)
{
    DimmConfig *dimm_cfg, *next_dimm_cfg;
    DimmBus *bus = DIMM_BUS(obj);
    QTAILQ_INIT(&bus->dimmconfig_list);
    QTAILQ_INIT(&bus->dimmlist);
    QTAILQ_INIT(&bus->dimm_hp_result_queue);

    QTAILQ_FOREACH_SAFE(dimm_cfg, &dimmconfig_list, nextdimmcfg, next_dimm_cfg) {
        QTAILQ_REMOVE(&dimmconfig_list, dimm_cfg, nextdimmcfg);
        QTAILQ_INSERT_TAIL(&bus->dimmconfig_list, dimm_cfg, nextdimmcfg);
    }
}

static const TypeInfo dimm_bus_info = {
    .name = TYPE_DIMM_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(DimmBus),
    .instance_init = dimm_bus_initfn,
    .class_init = dimm_bus_class_init,
};

void main_memory_bus_create(Object *parent)
{
    main_memory_bus = g_malloc0(dimm_bus_info.instance_size);
    main_memory_bus->qbus.glib_allocated = true;
    qbus_create_inplace(&main_memory_bus->qbus, TYPE_DIMM_BUS, DEVICE(parent),
                        "membus");
}

static void dimm_populate(DimmDevice *s)
{
    DeviceState *dev= (DeviceState*)s;
    MemoryRegion *new = NULL;

    new = g_malloc(sizeof(MemoryRegion));
    memory_region_init_ram(new, dev->id, s->size);
    vmstate_register_ram_global(new);
    memory_region_add_subregion(get_system_memory(), s->start, new);
    s->mr = new;
}

static void dimm_depopulate(DimmDevice *s)
{
    assert(s);
    vmstate_unregister_ram(s->mr, NULL);
    memory_region_del_subregion(get_system_memory(), s->mr);
    memory_region_destroy(s->mr);
    s->mr = NULL;
}

void dimm_config_create(char *id, uint64_t size, uint64_t node, uint32_t
        dimm_idx, uint32_t populated)
{
    DimmConfig *dimm_cfg;
    dimm_cfg = (DimmConfig*) g_malloc0(sizeof(DimmConfig));
    dimm_cfg->name = id;
    dimm_cfg->idx = dimm_idx;
    dimm_cfg->start = 0;
    dimm_cfg->size = size;
    dimm_cfg->node = node;
    dimm_cfg->populated = populated;

    QTAILQ_INSERT_TAIL(&dimmconfig_list, dimm_cfg, nextdimmcfg);
}

void dimm_bus_hotplug(dimm_hotplug_fn hotplug, dimm_hotplug_fn revert,
        DeviceState *qdev)
{
    DimmBus *bus = main_memory_bus;
    bus->qbus.allow_hotplug = 1;
    bus->dimm_hotplug_qdev = qdev;
    bus->dimm_hotplug = hotplug;
    bus->dimm_revert = revert;
}

static void dimm_plug_device(DimmDevice *slot)
{
    DimmBus *bus = main_memory_bus;

    dimm_populate(slot);
    if (bus->dimm_hotplug)
        bus->dimm_hotplug(bus->dimm_hotplug_qdev, slot, 1);
    slot->pending = DIMM_ADD_PENDING;
}

static int dimm_unplug_device(DeviceState *qdev)
{
    DimmBus *bus = main_memory_bus;

    if (bus->dimm_hotplug)
        bus->dimm_hotplug(bus->dimm_hotplug_qdev, DIMM(qdev), 0);
    DIMM(qdev)->pending = DIMM_REMOVE_PENDING;
    return 1;
}

static DimmConfig *dimmcfg_find_from_name(const char *name)
{
    DimmConfig *slot;
    DimmBus *bus = main_memory_bus;

    QTAILQ_FOREACH(slot, &bus->dimmconfig_list, nextdimmcfg) {
        if (!strcmp(slot->name, name)) {
            return slot;
        }
    }
    return NULL;
}

static DimmDevice *dimm_find_from_idx(uint32_t idx)
{
    DimmDevice *slot;
    DimmBus *bus = main_memory_bus;

    QTAILQ_FOREACH(slot, &bus->dimmlist, nextdimm) {
        if (slot->idx == idx) {
            return slot;
        }
    }
    return NULL;
}

void dimm_state_sync(void)
{
    DimmBus *bus = main_memory_bus;
    DimmDevice *slot;

    /* if a hot-remove operation is pending on reset, it means the hot-remove
     * operation has failed, but the guest hasn't notified us e.g. because the
     * guest does not provide _OST notifications. The device is still present on
     * the dimmbus, but the qemu and Seabios dimm bitmaps show this device as
     * unplugged. To avoid this inconsistency, we set the dimm bits to active
     * i.e. hot-plugged for each dimm present on the dimmbus.
     */
    QTAILQ_FOREACH(slot, &bus->dimmlist, nextdimm) {
        if (slot->pending == DIMM_REMOVE_PENDING) {
            if (bus->dimm_revert)
                bus->dimm_revert(bus->dimm_hotplug_qdev, slot, 0);
        }
    }
}

/* used to create a dimm device, only on incoming migration of a hotplugged
 * RAMBlock
 */
int dimm_add(char *id)
{
    DimmConfig *slotcfg = NULL;
    QemuOpts *devopts;
    char buf[256];

    if (!id) {
        fprintf(stderr, "ERROR %s invalid id\n",__FUNCTION__);
        return 1;
    }

    slotcfg = dimmcfg_find_from_name(id);

    if (!slotcfg) {
        fprintf(stderr, "%s no slot %s found\n", __FUNCTION__, id);
        return 1;
    }

    devopts = qemu_opts_create(qemu_find_opts("device"), id, 0, NULL);
    qemu_opt_set(devopts, "driver", "dimm");

    snprintf(buf, sizeof(buf), "%lu", slotcfg->size);
    qemu_opt_set(devopts, "size", buf);
    snprintf(buf, sizeof(buf), "%u", slotcfg->node);
    qemu_opt_set(devopts, "node", buf);
    qdev_device_add(devopts);

    return 0;
}

/* used to calculate physical address offsets for all dimms */
void dimm_calc_offsets(dimm_calcoffset_fn calcfn)
{
    DimmConfig *slot;
    QTAILQ_FOREACH(slot, &dimmconfig_list, nextdimmcfg) {
        if (!slot->start) {
            slot->start = calcfn(slot->size);
        }
    }
}

void setup_fwcfg_hp_dimms(uint64_t *fw_cfg_slots)
{
    DimmConfig *slot;

    QTAILQ_FOREACH(slot, &dimmconfig_list, nextdimmcfg) {
        assert(slot->start);
        fw_cfg_slots[3 * slot->idx] = cpu_to_le64(slot->start);
        fw_cfg_slots[3 * slot->idx + 1] = cpu_to_le64(slot->size);
        fw_cfg_slots[3 * slot->idx + 2] = cpu_to_le64(slot->node);
    }
}

uint64_t get_hp_memory_total(void)
{
    DimmBus *bus = main_memory_bus;
    DimmDevice *slot;
    uint64_t info = 0;

    QTAILQ_FOREACH(slot, &bus->dimmlist, nextdimm) {
        info += slot->size;
    }
    return info;
}

int64_t qmp_query_memory_total(Error **errp)
{
    uint64_t info;
    info = ram_size + get_hp_memory_total();

    return (int64_t)info;
}

void dimm_notify(uint32_t idx, uint32_t event)
{
    DimmBus *bus = main_memory_bus;
    DimmDevice *s;
    DimmConfig *slotcfg;
    struct dimm_hp_result *result;

    s = dimm_find_from_idx(idx);
    assert(s != NULL);
    result = g_malloc0(sizeof(*result));
    slotcfg = dimmcfg_find_from_name(DEVICE(s)->id);
    result->dimmname = slotcfg->name;
    result->ret = event;

    switch(event) {
        case DIMM_REMOVE_SUCCESS:
            dimm_depopulate(s);
            QTAILQ_REMOVE(&bus->dimmlist, s, nextdimm);
            qdev_simple_unplug_cb((DeviceState*)s);
            s->pending = DIMM_NO_PENDING;
            QTAILQ_INSERT_TAIL(&bus->dimm_hp_result_queue, result, next);
            break;
        case DIMM_REMOVE_FAIL:
            s->pending = DIMM_NO_PENDING;
            if (bus->dimm_revert)
                bus->dimm_revert(bus->dimm_hotplug_qdev, s, 0);
            QTAILQ_INSERT_TAIL(&bus->dimm_hp_result_queue, result, next);
            break;
        case DIMM_ADD_SUCCESS:
            s->pending = DIMM_NO_PENDING;
            QTAILQ_INSERT_TAIL(&bus->dimm_hp_result_queue, result, next);
            break;
        case DIMM_ADD_FAIL:
            dimm_depopulate(s);
            s->pending = DIMM_NO_PENDING;
            if (bus->dimm_revert)
                bus->dimm_revert(bus->dimm_hotplug_qdev, s, 1);
            QTAILQ_REMOVE(&bus->dimmlist, s, nextdimm);
            qdev_simple_unplug_cb((DeviceState*)s);
            QTAILQ_INSERT_TAIL(&bus->dimm_hp_result_queue, result, next);
            break;
        default:
            g_free(result);
            break;
    }
}

MemHpInfoList *qmp_query_memory_hotplug(Error **errp)
{
    DimmBus *bus = main_memory_bus;
    MemHpInfoList *head = NULL, *cur_item = NULL, *info;
    struct dimm_hp_result *item, *nextitem;

    QTAILQ_FOREACH_SAFE(item, &bus->dimm_hp_result_queue, next, nextitem) {

        info = g_malloc0(sizeof(*info));
        info->value = g_malloc0(sizeof(*info->value));
        info->value->dimm = g_malloc0(sizeof(char) * 32);
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
        strcpy(info->value->dimm, item->dimmname);
        /* XXX: waiting for the qapi to support GSList */
        if (!cur_item) {
            head = cur_item = info;
        } else {
            cur_item->next = info;
            cur_item = info;
        }

        /* hotplug notification copied to qmp list, delete original item */
        QTAILQ_REMOVE(&bus->dimm_hp_result_queue, item, next);
        g_free(item);
    }

    return head;
}

static int dimm_init(DeviceState *s)
{
    DimmBus *bus = main_memory_bus;
    DimmDevice *slot;
    DimmConfig *slotcfg;

    slot = DIMM(s);
    slot->mr = NULL;

    slotcfg = dimmcfg_find_from_name(s->id);

    if (!slotcfg) {
        fprintf(stderr, "%s no config for slot %s found\n",
                __FUNCTION__, s->id);
        return 1;
    }

    slot->idx = slotcfg->idx;
    assert(slotcfg->start);
    slot->start = slotcfg->start;
    slot->size = slotcfg->size;
    slot->node = slotcfg->node;
    slot->pending = DIMM_NO_PENDING;

    QTAILQ_INSERT_TAIL(&bus->dimmlist, slot, nextdimm);
    dimm_plug_device(slot);

    return 0;
}


static void dimm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->props = dimm_properties;
    dc->unplug = dimm_unplug_device;
    dc->bus_type = TYPE_DIMM_BUS;
    dc->init = dimm_init;
}

static TypeInfo dimm_info = {
    .name          = TYPE_DIMM,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(DimmDevice),
    .class_init    = dimm_class_init,
};

static void dimm_register_types(void)
{
    type_register_static(&dimm_bus_info);
    type_register_static(&dimm_info);
}

type_init(dimm_register_types)
