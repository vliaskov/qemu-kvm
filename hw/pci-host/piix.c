/*
 * QEMU i440FX/PIIX3 PCI Bridge Emulation
 *
 * Copyright (c) 2006 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "hw/hw.h"
#include "hw/i386/pc.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_host.h"
#include "hw/isa/isa.h"
#include "hw/sysbus.h"
#include "qemu/range.h"
#include "hw/xen/xen.h"
#include "hw/pci-host/pam.h"
#include "sysemu/sysemu.h"
#include "hw/pci-host/piix_pci.h"

/* return the global irq number corresponding to a given device irq
   pin. We could also use the bus number to have a more precise
   mapping. */
int pci_slot_get_pirq(PCIDevice *pci_dev, int pci_intx)
{
    int slot_addend;
    slot_addend = (pci_dev->devfn >> 3) - 1;
    return (pci_intx + slot_addend) & 3;
}

static void i440fx_update_memory_mappings(PCII440FXState *d)
{
    int i;

    memory_region_transaction_begin();
    for (i = 0; i < 13; i++) {
        pam_update(&d->pam_regions[i], i,
                   d->dev.config[I440FX_PAM + ((i + 1) / 2)]);
    }
    smram_update(&d->smram_region, d->dev.config[I440FX_SMRAM], d->smm_enabled);
    memory_region_transaction_commit();
}

static void i440fx_set_smm(int val, void *arg)
{
    PCII440FXState *d = arg;

    memory_region_transaction_begin();
    smram_set_smm(&d->smm_enabled, val, d->dev.config[I440FX_SMRAM],
                  &d->smram_region);
    memory_region_transaction_commit();
}


static void i440fx_write_config(PCIDevice *dev,
                                uint32_t address, uint32_t val, int len)
{
    PCII440FXState *d = I440FX_PCI_DEVICE(dev);

    /* XXX: implement SMRAM.D_LOCK */
    pci_default_write_config(dev, address, val, len);
    if (ranges_overlap(address, len, I440FX_PAM, I440FX_PAM_SIZE) ||
        range_covers_byte(address, len, I440FX_SMRAM)) {
        i440fx_update_memory_mappings(d);
    }
}

static int i440fx_load_old(QEMUFile* f, void *opaque, int version_id)
{
    PCII440FXState *d = opaque;
    int ret, i;

    ret = pci_device_load(&d->dev, f);
    if (ret < 0)
        return ret;
    i440fx_update_memory_mappings(d);
    qemu_get_8s(f, &d->smm_enabled);

    if (version_id == 2) {
        for (i = 0; i < PIIX_NUM_PIRQS; i++) {
            qemu_get_be32(f); /* dummy load for compatibility */
        }
    }

    return 0;
}

static int i440fx_post_load(void *opaque, int version_id)
{
    PCII440FXState *d = opaque;

    i440fx_update_memory_mappings(d);
    return 0;
}

static const VMStateDescription vmstate_i440fx = {
    .name = "I440FX",
    .version_id = 3,
    .minimum_version_id = 3,
    .minimum_version_id_old = 1,
    .load_state_old = i440fx_load_old,
    .post_load = i440fx_post_load,
    .fields      = (VMStateField []) {
        VMSTATE_PCI_DEVICE(dev, PCII440FXState),
        VMSTATE_UINT8(smm_enabled, PCII440FXState),
        VMSTATE_END_OF_LIST()
    }
};

static void i440fx_pcihost_initfn(Object *obj)
{
    I440FXState *s = I440FX_HOST_DEVICE(obj);
    object_initialize(&s->mch, TYPE_I440FX_PCI_DEVICE);
    object_property_add_child(OBJECT(s), "mch", OBJECT(&s->mch), NULL);
}

static int i440fx_pcihost_init(SysBusDevice *dev)
{
    PCIHostState *phb = PCI_HOST_BRIDGE(dev);
    I440FXState *s = I440FX_HOST_DEVICE(dev);
    DeviceState *mch_qdev = DEVICE(&s->mch);
    PCIBus *b;

    memory_region_init_io(&phb->conf_mem, &pci_host_conf_le_ops, phb,
                           "pci-conf-idx", 4);
    sysbus_add_io(dev, 0xcf8, &phb->conf_mem);
    sysbus_init_ioports(&phb->busdev, 0xcf8, 4);
    memory_region_init_io(&phb->data_mem, &pci_host_data_le_ops, phb,
                           "pci-conf-data", 4);


    memory_region_init_io(&phb->data_mem, &pci_host_data_le_ops, s,
                          "pci-conf-data", 4);
    sysbus_add_io(dev, 0xcfc, &phb->data_mem);
    sysbus_init_ioports(&phb->busdev, 0xcfc, 4);

    b = pci_bus_new(DEVICE(phb), "pci.0", s->mch.pci_address_space,
                    s->mch.address_space_io, 0, TYPE_PCI_BUS);
    s->parent_obj.bus = b;

    qdev_set_parent_bus(mch_qdev, BUS(b));
    qdev_init_nofail(mch_qdev);

    return 0;
}

static int i440fx_initfn(PCIDevice *dev)
{
    int i;
    PCII440FXState *d = DO_UPCAST(PCII440FXState, dev, dev);
    hwaddr pci_hole64_size;
 
    d->dev.config[I440FX_SMRAM] = 0x02;

    cpu_smm_register(&i440fx_set_smm, d);

    pci_hole64_size = (sizeof(hwaddr) == 4 ? 0 :
                       ((uint64_t)1 << 62));
    memory_region_init_alias(&d->pci_hole, "pci-hole", d->pci_address_space,
                             d->below_4g_mem_size,
                             0x100000000LL - d->below_4g_mem_size);
    memory_region_add_subregion(d->system_memory, d->below_4g_mem_size,
            &d->pci_hole);
    memory_region_init_alias(&d->pci_hole_64bit, "pci-hole64",
                             d->pci_address_space,
                             0x100000000LL + d->above_4g_mem_size,
                             pci_hole64_size);
    if (pci_hole64_size) {
        memory_region_add_subregion(d->system_memory,
                                    0x100000000LL + d->above_4g_mem_size,
                                    &d->pci_hole_64bit);
    }
    memory_region_init_alias(&d->smram_region, "smram-region",
                             d->pci_address_space, 0xa0000, 0x20000);
    memory_region_add_subregion_overlap(d->system_memory, 0xa0000,
                                        &d->smram_region, 1);
    memory_region_set_enabled(&d->smram_region, false);
    init_pam(d->ram_memory, d->system_memory, d->pci_address_space,
             &d->pam_regions[0], PAM_BIOS_BASE, PAM_BIOS_SIZE);
    for (i = 0; i < 12; ++i) {
        init_pam(d->ram_memory, d->system_memory, d->pci_address_space,
                 &d->pam_regions[i+1], PAM_EXPAN_BASE + i * PAM_EXPAN_SIZE,
                 PAM_EXPAN_SIZE);
    }

    d->dev.config[0x57] = d->below_4g_mem_size;
    i440fx_update_memory_mappings(d);

    return 0;
}

/* PIIX3 PCI to ISA bridge */
void piix3_set_irq_pic(PIIX3State *piix3, int pic_irq)
{
    qemu_set_irq(piix3->pic[pic_irq],
                 !!(piix3->pic_levels &
                    (((1ULL << PIIX_NUM_PIRQS) - 1) <<
                     (pic_irq * PIIX_NUM_PIRQS))));
}

static void piix3_set_irq_level(PIIX3State *piix3, int pirq, int level)
{
    int pic_irq;
    uint64_t mask;

    pic_irq = piix3->dev.config[PIIX_PIRQC + pirq];
    if (pic_irq >= PIIX_NUM_PIC_IRQS) {
        return;
    }

    mask = 1ULL << ((pic_irq * PIIX_NUM_PIRQS) + pirq);
    piix3->pic_levels &= ~mask;
    piix3->pic_levels |= mask * !!level;

    piix3_set_irq_pic(piix3, pic_irq);
}

void piix3_set_irq(void *opaque, int pirq, int level)
{
    PIIX3State *piix3 = opaque;
    piix3_set_irq_level(piix3, pirq, level);
}

PCIINTxRoute piix3_route_intx_pin_to_irq(void *opaque, int pin)
{
    PIIX3State *piix3 = opaque;
    int irq = piix3->dev.config[PIIX_PIRQC + pin];
    PCIINTxRoute route;

    if (irq < PIIX_NUM_PIC_IRQS) {
        route.mode = PCI_INTX_ENABLED;
        route.irq = irq;
    } else {
        route.mode = PCI_INTX_DISABLED;
        route.irq = -1;
    }
    return route;
}

/* irq routing is changed. so rebuild bitmap */
static void piix3_update_irq_levels(PIIX3State *piix3)
{
    int pirq;

    piix3->pic_levels = 0;
    for (pirq = 0; pirq < PIIX_NUM_PIRQS; pirq++) {
        piix3_set_irq_level(piix3, pirq,
                            pci_bus_get_irq_level(piix3->dev.bus, pirq));
    }
}

static void piix3_write_config(PCIDevice *dev,
                               uint32_t address, uint32_t val, int len)
{
    pci_default_write_config(dev, address, val, len);
    if (ranges_overlap(address, len, PIIX_PIRQC, 4)) {
        PIIX3State *piix3 = DO_UPCAST(PIIX3State, dev, dev);
        int pic_irq;

        pci_bus_fire_intx_routing_notifier(piix3->dev.bus);
        piix3_update_irq_levels(piix3);
        for (pic_irq = 0; pic_irq < PIIX_NUM_PIC_IRQS; pic_irq++) {
            piix3_set_irq_pic(piix3, pic_irq);
        }
    }
}

static void piix3_write_config_xen(PCIDevice *dev,
                               uint32_t address, uint32_t val, int len)
{
    xen_piix_pci_write_config_client(address, val, len);
    piix3_write_config(dev, address, val, len);
}

static void piix3_reset(void *opaque)
{
    PIIX3State *d = opaque;
    uint8_t *pci_conf = d->dev.config;

    pci_conf[0x04] = 0x07; /* master, memory and I/O */
    pci_conf[0x05] = 0x00;
    pci_conf[0x06] = 0x00;
    pci_conf[0x07] = 0x02; /* PCI_status_devsel_medium */
    pci_conf[0x4c] = 0x4d;
    pci_conf[0x4e] = 0x03;
    pci_conf[0x4f] = 0x00;
    pci_conf[0x60] = 0x80;
    pci_conf[0x61] = 0x80;
    pci_conf[0x62] = 0x80;
    pci_conf[0x63] = 0x80;
    pci_conf[0x69] = 0x02;
    pci_conf[0x70] = 0x80;
    pci_conf[0x76] = 0x0c;
    pci_conf[0x77] = 0x0c;
    pci_conf[0x78] = 0x02;
    pci_conf[0x79] = 0x00;
    pci_conf[0x80] = 0x00;
    pci_conf[0x82] = 0x00;
    pci_conf[0xa0] = 0x08;
    pci_conf[0xa2] = 0x00;
    pci_conf[0xa3] = 0x00;
    pci_conf[0xa4] = 0x00;
    pci_conf[0xa5] = 0x00;
    pci_conf[0xa6] = 0x00;
    pci_conf[0xa7] = 0x00;
    pci_conf[0xa8] = 0x0f;
    pci_conf[0xaa] = 0x00;
    pci_conf[0xab] = 0x00;
    pci_conf[0xac] = 0x00;
    pci_conf[0xae] = 0x00;

    d->pic_levels = 0;
    d->rcr = 0;
}

static int piix3_post_load(void *opaque, int version_id)
{
    PIIX3State *piix3 = opaque;
    piix3_update_irq_levels(piix3);
    return 0;
}

static void piix3_pre_save(void *opaque)
{
    int i;
    PIIX3State *piix3 = opaque;

    for (i = 0; i < ARRAY_SIZE(piix3->pci_irq_levels_vmstate); i++) {
        piix3->pci_irq_levels_vmstate[i] =
            pci_bus_get_irq_level(piix3->dev.bus, i);
    }
}

static bool piix3_rcr_needed(void *opaque)
{
    PIIX3State *piix3 = opaque;

    return (piix3->rcr != 0);
}

static const VMStateDescription vmstate_piix3_rcr = {
    .name = "PIIX3/rcr",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField []) {
        VMSTATE_UINT8(rcr, PIIX3State),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_piix3 = {
    .name = "PIIX3",
    .version_id = 3,
    .minimum_version_id = 2,
    .minimum_version_id_old = 2,
    .post_load = piix3_post_load,
    .pre_save = piix3_pre_save,
    .fields      = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, PIIX3State),
        VMSTATE_INT32_ARRAY_V(pci_irq_levels_vmstate, PIIX3State,
                              PIIX_NUM_PIRQS, 3),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (VMStateSubsection[]) {
        {
            .vmsd = &vmstate_piix3_rcr,
            .needed = piix3_rcr_needed,
        },
        { 0 }
    }
};


static void rcr_write(void *opaque, hwaddr addr, uint64_t val, unsigned len)
{
    PIIX3State *d = opaque;

    if (val & 4) {
        qemu_system_reset_request();
        return;
    }
    d->rcr = val & 2; /* keep System Reset type only */
}

static uint64_t rcr_read(void *opaque, hwaddr addr, unsigned len)
{
    PIIX3State *d = opaque;

    return d->rcr;
}

static const MemoryRegionOps rcr_ops = {
    .read = rcr_read,
    .write = rcr_write,
    .endianness = DEVICE_LITTLE_ENDIAN
};

static int piix3_initfn(PCIDevice *dev)
{
    PIIX3State *d = DO_UPCAST(PIIX3State, dev, dev);

    isa_bus_new(DEVICE(d), pci_address_space_io(dev));

    memory_region_init_io(&d->rcr_mem, &rcr_ops, d, "piix3-reset-control", 1);
    memory_region_add_subregion_overlap(pci_address_space_io(dev), RCR_IOPORT,
                                        &d->rcr_mem, 1);

    qemu_register_reset(piix3_reset, d);
    return 0;
}

static void piix3_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    dc->desc        = "ISA bridge";
    dc->vmsd        = &vmstate_piix3;
    dc->no_user     = 1,
    k->no_hotplug   = 1;
    k->init         = piix3_initfn;
    k->config_write = piix3_write_config;
    k->vendor_id    = PCI_VENDOR_ID_INTEL;
    /* 82371SB PIIX3 PCI-to-ISA bridge (Step A1) */
    k->device_id    = PCI_DEVICE_ID_INTEL_82371SB_0;
    k->class_id     = PCI_CLASS_BRIDGE_ISA;
}

static const TypeInfo piix3_info = {
    .name          = "PIIX3",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PIIX3State),
    .class_init    = piix3_class_init,
};

static void piix3_xen_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    dc->desc        = "ISA bridge";
    dc->vmsd        = &vmstate_piix3;
    dc->no_user     = 1;
    k->no_hotplug   = 1;
    k->init         = piix3_initfn;
    k->config_write = piix3_write_config_xen;
    k->vendor_id    = PCI_VENDOR_ID_INTEL;
    /* 82371SB PIIX3 PCI-to-ISA bridge (Step A1) */
    k->device_id    = PCI_DEVICE_ID_INTEL_82371SB_0;
    k->class_id     = PCI_CLASS_BRIDGE_ISA;
};

static const TypeInfo piix3_xen_info = {
    .name          = "PIIX3-xen",
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PIIX3State),
    .class_init    = piix3_xen_class_init,
};

static void i440fx_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->no_hotplug = 1;
    k->init = i440fx_initfn;
    k->config_write = i440fx_write_config;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = PCI_DEVICE_ID_INTEL_82441;
    k->revision = 0x02;
    k->class_id = PCI_CLASS_BRIDGE_HOST;
    dc->desc = "Host bridge";
    dc->no_user = 1;
    dc->vmsd = &vmstate_i440fx;
}

static const TypeInfo i440fx_info = {
    .name          = TYPE_I440FX_PCI_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCII440FXState),
    .class_init    = i440fx_class_init,
};

static void i440fx_pcihost_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = i440fx_pcihost_init;
    dc->fw_name = "pci";
    dc->no_user = 1;
}

static const TypeInfo i440fx_pcihost_info = {
    .name          = TYPE_I440FX_HOST_DEVICE,
    .parent        = TYPE_PCI_HOST_BRIDGE,
    .instance_size = sizeof(I440FXState),
    .instance_init = i440fx_pcihost_initfn,
    .class_init    = i440fx_pcihost_class_init,
};

static void i440fx_register_types(void)
{
    type_register_static(&i440fx_info);
    type_register_static(&piix3_info);
    type_register_static(&piix3_xen_info);
    type_register_static(&i440fx_pcihost_info);
}

type_init(i440fx_register_types)
