/*
 * QEMU Host Memory Backend
 *
 * Copyright (C) 2013 Red Hat Inc
 *
 * Authors:
 *   Igor Mammedov <imammedo@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "sysemu/hostmem.h"
#include "sysemu/sysemu.h"
#include "qapi/visitor.h"
#include "qapi/qmp/qerror.h"
#include "qemu/config-file.h"
#include "qom/object_interfaces.h"

static void
hostmemory_backend_get_size(Object *obj, Visitor *v, void *opaque,
                            const char *name, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    uint64_t value = backend->size;

    visit_type_size(v, &value, name, errp);
}

static void
hostmemory_backend_set_size(Object *obj, Visitor *v, void *opaque,
                            const char *name, Error **errp)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);
    uint64_t value;

    if (memory_region_size(&backend->mr)) {
        error_setg(errp, "cannot change property value\n");
        return;
    }

    visit_type_size(v, &value, name, errp);
    if (error_is_set(errp)) {
        return;
    }
    if (!value) {
        error_setg(errp, "Property '%s.%s' doesn't take value '%" PRIu64 "'",
                   object_get_typename(obj), name , value);
        return;
    }
    backend->size = value;
}

static void hostmemory_backend_initfn(Object *obj)
{
    object_property_add(obj, "size", "int",
                        hostmemory_backend_get_size,
                        hostmemory_backend_set_size, NULL, NULL, NULL);
}

static void hostmemory_backend_finalize(Object *obj)
{
    HostMemoryBackend *backend = MEMORY_BACKEND(obj);

    if (memory_region_size(&backend->mr)) {
        memory_region_destroy(&backend->mr);
    }
}

static void
hostmemory_backend_memory_init(UserCreatable *uc, Error **errp)
{
    error_setg(errp, "memory_init is not implemented for type [%s]",
               object_get_typename(OBJECT(uc)));
}

MemoryRegion *
host_memory_backend_get_memory(HostMemoryBackend *backend, Error **errp)
{
    return memory_region_size(&backend->mr) ? &backend->mr : NULL;
}

static void
hostmemory_backend_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);

    ucc->complete = hostmemory_backend_memory_init;
}

static const TypeInfo hostmemory_backend_info = {
    .name = TYPE_MEMORY_BACKEND,
    .parent = TYPE_OBJECT,
    .abstract = true,
    .class_size = sizeof(HostMemoryBackendClass),
    .class_init = hostmemory_backend_class_init,
    .instance_size = sizeof(HostMemoryBackend),
    .instance_init = hostmemory_backend_initfn,
    .instance_finalize = hostmemory_backend_finalize,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void register_types(void)
{
    type_register_static(&hostmemory_backend_info);
}

type_init(register_types);
