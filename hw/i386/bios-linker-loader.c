/* Dynamic linker/loader of ACPI tables
 *
 * Copyright (C) 2013 Red Hat Inc
 *
 * Author: Michael S. Tsirkin <mst@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/i386/bios-linker-loader.h"

#include <string.h>
#include <assert.h>
#include "qemu/bswap.h"

#define BIOS_LINKER_LOADER_FILESZ 56

struct BiosLinkerLoaderEntry {
    uint32_t command;
    union {
        /*
         * COMMAND_ALLOCATE - allocate a table from @alloc_file
         * subject to @alloc_align alignment (must be power of 2)
         * and @alloc_zone (can be HIGH or FSEG) requirements.
         *
         * Must appear exactly once for each file, and before
         * this file is referenced by any other command.
         */
        struct {
            char alloc_file[BIOS_LINKER_LOADER_FILESZ];
            uint32_t alloc_align;
            uint8_t alloc_zone;
        };

        /*
         * COMMAND_ADD_POINTER - patch the table (originating from
         * @dest_file) at @pointer_offset, by adding a pointer to the table
         * originating from @src_file. 1,2,4 or 8 byte unsigned
         * addition is used depending on @pointer_size.
         */
        struct {
            char pointer_dest_file[BIOS_LINKER_LOADER_FILESZ];
            char pointer_src_file[BIOS_LINKER_LOADER_FILESZ];
            uint32_t pointer_offset;
            uint8_t pointer_size;
        };

        /*
         * COMMAND_ADD_CHECKSUM - calculate checksum of the range specified by
         * @cksum_start and @cksum_length fields,
         * and then add the value at @cksum_offset.
         * Checksum simply sums -X for each byte X in the range
         * using 8-bit math.
         */
        struct {
            char cksum_file[BIOS_LINKER_LOADER_FILESZ];
            uint32_t cksum_offset;
            uint32_t cksum_start;
            uint32_t cksum_length;
        };

        /* padding */
        char pad[124];
    };
};
typedef struct BiosLinkerLoaderEntry BiosLinkerLoaderEntry;

enum {
    BIOS_LINKER_LOADER_COMMAND_ALLOCATE     = 0x1,
    BIOS_LINKER_LOADER_COMMAND_ADD_POINTER  = 0x2,
    BIOS_LINKER_LOADER_COMMAND_ADD_CHECKSUM = 0x3,
};

enum {
    BIOS_LINKER_LOADER_ALLOC_ZONE_HIGH = 0x1,
    BIOS_LINKER_LOADER_ALLOC_ZONE_FSEG = 0x2,
};

GArray *bios_linker_init(void)
{
    return g_array_new(false, true /* clear */, sizeof(BiosLinkerLoaderEntry));
}

/* Free linker wrapper and return the linker array. */
void *bios_linker_cleanup(GArray *linker)
{
    return g_array_free(linker, false);
}

void bios_linker_alloc(GArray *linker,
                       const char *file,
                       uint32_t alloc_align,
                       bool alloc_fseg)
{
    BiosLinkerLoaderEntry entry;

    memset(&entry, 0, sizeof entry);
    strncpy(entry.alloc_file, file, sizeof entry.alloc_file - 1);
    entry.command = cpu_to_le32(BIOS_LINKER_LOADER_COMMAND_ALLOCATE);
    entry.alloc_align = cpu_to_le32(alloc_align);
    entry.alloc_zone = cpu_to_le32(alloc_fseg ?
                                    BIOS_LINKER_LOADER_ALLOC_ZONE_FSEG :
                                    BIOS_LINKER_LOADER_ALLOC_ZONE_HIGH);

    /* Alloc entries must come first, so prepend them */
    g_array_prepend_val(linker, entry);
}

void bios_linker_add_checksum(GArray *linker, const char *file, void *table,
                              void *start, unsigned size, uint8_t *checksum)
{
    BiosLinkerLoaderEntry entry;

    memset(&entry, 0, sizeof entry);
    strncpy(entry.cksum_file, file, sizeof entry.cksum_file - 1);
    entry.command = cpu_to_le32(BIOS_LINKER_LOADER_COMMAND_ADD_CHECKSUM);
    entry.cksum_offset = cpu_to_le32(checksum - (uint8_t *)table);
    entry.cksum_start = cpu_to_le32((uint8_t *)start - (uint8_t *)table);
    entry.cksum_length = cpu_to_le32(size);

    g_array_append_val(linker, entry);
}

void bios_linker_add_pointer(GArray *linker,
                             const char *dest_file,
                             const char *src_file,
                             GArray *table, void *pointer,
                             uint8_t pointer_size)
{
    BiosLinkerLoaderEntry entry;

    memset(&entry, 0, sizeof entry);
    strncpy(entry.pointer_dest_file, dest_file,
            sizeof entry.pointer_dest_file - 1);
    strncpy(entry.pointer_src_file, src_file,
            sizeof entry.pointer_src_file - 1);
    entry.command = cpu_to_le32(BIOS_LINKER_LOADER_COMMAND_ADD_POINTER);
    entry.pointer_offset = cpu_to_le32((gchar *)pointer - table->data);
    entry.pointer_size = pointer_size;
    assert(pointer_size == 1 || pointer_size == 2 ||
           pointer_size == 4 || pointer_size == 8);

    g_array_append_val(linker, entry);
}
