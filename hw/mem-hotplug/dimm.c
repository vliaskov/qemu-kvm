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
#include "hw/qdev.h"
#include "hw/mem-hotplug/dimm.h"
#include <time.h>
#include "qmp-commands.h"

/* the following list is used to hold dimm config info before machine
 * is initialized. After machine init, the list is not used anymore.*/
static DimmConfiglist dimmconfig_list =
       QTAILQ_HEAD_INITIALIZER(dimmconfig_list);

/* the list of memory buses */
static QLIST_HEAD(, DimmBus) memory_buses;

static void dimmbus_dev_print(Monitor *mon, DeviceState *dev, int indent);
static char *dimmbus_get_fw_dev_path(DeviceState *dev);

static Property dimm_properties[] = {
    DEFINE_PROP_UINT64("start", DimmDevice, start, 0),
    DEFINE_PROP_SIZE("size", DimmDevice, size, DEFAULT_DIMMSIZE),
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
    DimmBus *bus = DIMM_BUS(obj);
    QTAILQ_INIT(&bus->dimmconfig_list);
    QTAILQ_INIT(&bus->dimmlist);
}

static const TypeInfo dimm_bus_info = {
    .name = TYPE_DIMM_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(DimmBus),
    .instance_init = dimm_bus_initfn,
    .class_init = dimm_bus_class_init,
};

DimmBus *dimm_bus_create(Object *parent, const char *name, uint32_t max_dimms,
    dimm_calcoffset_fn pmc_set_offset)
{
    DimmBus *memory_bus;
    DimmConfig *dimm_cfg, *next_cfg;
    uint32_t num_dimms = 0;

    memory_bus = g_malloc0(dimm_bus_info.instance_size);
    memory_bus->qbus.name = name ? g_strdup(name) : "membus.0";
    qbus_create_inplace(&memory_bus->qbus, TYPE_DIMM_BUS, DEVICE(parent),
                         name);

    QTAILQ_FOREACH_SAFE(dimm_cfg, &dimmconfig_list, nextdimmcfg, next_cfg) {
        if (!strcmp(memory_bus->qbus.name, dimm_cfg->bus_name)) {
            if (max_dimms && (num_dimms == max_dimms)) {
                fprintf(stderr, "Bus %s can only accept %u number of DIMMs\n",
                        name, max_dimms);
            }
            QTAILQ_REMOVE(&dimmconfig_list, dimm_cfg, nextdimmcfg);
            QTAILQ_INSERT_TAIL(&memory_bus->dimmconfig_list, dimm_cfg,
                    nextdimmcfg);

            dimm_cfg->start = pmc_set_offset(DEVICE(parent), dimm_cfg->size);
            num_dimms++;
        }
    }
    QLIST_INSERT_HEAD(&memory_buses, memory_bus, next);
    return memory_bus;
}

static void dimm_populate(DimmDevice *s)
{
    DeviceState *dev = (DeviceState *)s;
    MemoryRegion *new = NULL;

    new = g_malloc(sizeof(MemoryRegion));
    memory_region_init_ram(new, dev->id, s->size);
    vmstate_register_ram_global(new);
    memory_region_add_subregion(get_system_memory(), s->start, new);
    s->mr = new;
}

void dimm_config_create(char *id, uint64_t size, const char *bus, uint64_t node,
        uint32_t dimm_idx)
{
    DimmConfig *dimm_cfg;
    dimm_cfg = (DimmConfig *) g_malloc0(sizeof(DimmConfig));
    dimm_cfg->name = strdup(id);
    dimm_cfg->bus_name = strdup(bus);
    dimm_cfg->idx = dimm_idx;
    dimm_cfg->start = 0;
    dimm_cfg->size = size;
    dimm_cfg->node = node;

    QTAILQ_INSERT_TAIL(&dimmconfig_list, dimm_cfg, nextdimmcfg);
}

void dimm_bus_hotplug(dimm_hotplug_fn hotplug, DeviceState *qdev)
{
    DimmBus *bus;
    QLIST_FOREACH(bus, &memory_buses, next) {
        assert(bus);
        bus->qbus.allow_hotplug = 1;
        bus->dimm_hotplug_qdev = qdev;
        bus->dimm_hotplug = hotplug;
    }
}

static void dimm_plug_device(DimmDevice *slot)
{
    DimmBus *bus = DIMM_BUS(qdev_get_parent_bus(&slot->qdev));

    dimm_populate(slot);
    if (bus->dimm_hotplug) {
        bus->dimm_hotplug(bus->dimm_hotplug_qdev, slot, 1);
    }
}

static int dimm_unplug_device(DeviceState *qdev)
{
    return 1;
}

static DimmConfig *dimmcfg_find_from_name(DimmBus *bus, const char *name)
{
    DimmConfig *slot;

    QTAILQ_FOREACH(slot, &bus->dimmconfig_list, nextdimmcfg) {
        if (!strcmp(slot->name, name)) {
            return slot;
        }
    }
    return NULL;
}

void dimm_setup_fwcfg_layout(uint64_t *fw_cfg_slots)
{
    DimmConfig *slot;
    DimmBus *bus;

    QLIST_FOREACH(bus, &memory_buses, next) {
        QTAILQ_FOREACH(slot, &bus->dimmconfig_list, nextdimmcfg) {
            assert(slot->start);
            fw_cfg_slots[3 * slot->idx] = cpu_to_le64(slot->start);
            fw_cfg_slots[3 * slot->idx + 1] = cpu_to_le64(slot->size);
            fw_cfg_slots[3 * slot->idx + 2] = cpu_to_le64(slot->node);
        }
    }
}

uint64_t get_hp_memory_total(void)
{
    DimmBus *bus;
    DimmDevice *slot;
    uint64_t info = 0;

    QLIST_FOREACH(bus, &memory_buses, next) {
        QTAILQ_FOREACH(slot, &bus->dimmlist, nextdimm) {
            info += slot->size;
        }
    }
    return info;
}

static int dimm_init(DeviceState *s)
{
    DimmBus *bus = DIMM_BUS(qdev_get_parent_bus(s));
    DimmDevice *slot;
    DimmConfig *slotcfg;

    slot = DIMM(s);
    slot->mr = NULL;

    slotcfg = dimmcfg_find_from_name(bus, s->id);

    if (!slotcfg) {
        fprintf(stderr, "%s FIXME no config for slot %s found, dimm should be deleted\n",
                __func__, s->id);
        /* qmp_device_del(s->id, NULL); */
        return 1;
    }

    slot->idx = slotcfg->idx;
    assert(slotcfg->start);
    slot->start = slotcfg->start;
    slot->size = slotcfg->size;
    slot->node = slotcfg->node;

    QTAILQ_INSERT_TAIL(&bus->dimmlist, slot, nextdimm);
    dimm_plug_device(slot);

    return 0;
}


static void dimm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->props = dimm_properties;
    dc->unplug = dimm_unplug_device;
    dc->init = dimm_init;
    dc->bus_type = TYPE_DIMM_BUS;
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
