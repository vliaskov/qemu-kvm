#include "hw/i386/pc.h"
#include "hw/isa/isa_pc.h"
#include "hw/loader.h"

static void isa_pc_realize(DeviceState *dev, Error **errp)
{
    ISAPc *isapc = ISA_PC(dev);

    g_assert(isapc->address_space_mem != NULL);
    g_assert(isapc->address_space_io != NULL);
    g_assert(isapc->ram_size > 0);

    isapc->bus = isa_bus_new(NULL, isapc->address_space_io);

    /* Allocate RAM.  We allocate it as a single memory region and use
     * aliases to address portions of it, mostly for backwards compatibility
     * with older qemus that used qemu_ram_alloc().
     */
    memory_region_init_ram(&isapc->ram, "pc.ram", isapc->ram_size);
    vmstate_register_ram_global(&isapc->ram);
    memory_region_add_subregion(isapc->address_space_mem, 0, &isapc->ram);

    pc_system_firmware_init(isapc->address_space_mem);

    memory_region_init_ram(&isapc->option_roms, "pc.rom", PC_ROM_SIZE);
    vmstate_register_ram_global(&isapc->option_roms);
    memory_region_add_subregion_overlap(isapc->address_space_mem,
                                        PC_ROM_MIN_VGA,
                                        &isapc->option_roms,
                                        1);
}

static void isa_pc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = isa_pc_realize;
    dc->no_user = 1;
}

static const TypeInfo isa_pc_type_info = {
    .name = TYPE_ISA_PC,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(ISAPc),
    .class_init = isa_pc_class_init,
};

static void isa_pc_register_types(void)
{
    type_register_static(&isa_pc_type_info);
}

type_init(isa_pc_register_types)
