#ifndef ISA_PC_H
#define ISA_PC_H

#include "qom/object.h"
#include "exec/memory.h"
#include "hw/isa/isa.h"

#define TYPE_ISA_PC "isa-pc"
#define ISA_PC(obj) OBJECT_CHECK(ISAPc, (obj), TYPE_ISA_PC)

typedef struct ISAPc ISAPc;

struct ISAPc {
    /*< private >*/
    Object parent_obj;
    /*< public >*/

    ISABus *bus;

    MemoryRegion *address_space_mem;
    MemoryRegion *address_space_io;
    MemoryRegion option_roms;
    MemoryRegion ram;
    ram_addr_t ram_size;
};

#endif
