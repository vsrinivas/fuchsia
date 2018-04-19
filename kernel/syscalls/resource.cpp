// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>

#include <fbl/ref_ptr.h>
#include <object/channel_dispatcher.h>
#include <object/handle.h>
#include <object/process_dispatcher.h>
#include <object/resource_dispatcher.h>

#include "priv.h"

// Create a new resource, child of the provided resource.
// On success, a new resource is created and handle is returned
// in |resource_out|.
//
// For more information on resources see docs/objects/resource.md
//
// The range low:high is inclusive on both ends, high must be
// greater than or equal low.
//
// |parent_rsrc| must be a resource of kind ZX_RSRC_KIND_ROOT. |base|
// and detail an inclusive range from |base| to |base| + |size| for
// the child resource.
zx_status_t sys_resource_create(zx_handle_t parent_rsrc,
                                uint32_t options,
                                uint64_t base,
                                size_t size,
                                user_in_ptr<const char> _name,
                                size_t name_size,
                                user_out_handle* resource_out) {
    auto up = ProcessDispatcher::GetCurrent();

    // Obtain the parent Resource
    // WRITE access is required to create a child resource
    zx_status_t status;
    fbl::RefPtr<ResourceDispatcher> parent;
    status = up->GetDispatcherWithRights(parent_rsrc, ZX_RIGHT_WRITE, &parent);
    if (status) {
        return status;
    }

    // Only holders of the root resource are permitted to create resources using this syscall.
    if (parent->get_kind() != ZX_RSRC_KIND_ROOT) {
        return ZX_ERR_ACCESS_DENIED;
    }

    uint32_t kind = ZX_RSRC_EXTRACT_KIND(options);
    uint32_t flags = ZX_RSRC_EXTRACT_FLAGS(options);
    if ((kind >= ZX_RSRC_KIND_COUNT) || (flags & ~ZX_RSRC_FLAGS_MASK)) {
        return ZX_ERR_INVALID_ARGS;
    }

    // Extract the name from userspace if one was provided.
    char name[ZX_MAX_NAME_LEN];
    size_t namesize = MIN(name_size,  ZX_MAX_NAME_LEN - 1);
    if (name_size > 0) {
        if (_name.copy_array_from_user(name, namesize) != ZX_OK) {
            return ZX_ERR_INVALID_ARGS;
        }
    }

    // Create a new Resource
    zx_rights_t rights;
    fbl::RefPtr<ResourceDispatcher> child;
    status = ResourceDispatcher::Create(&child, &rights, kind, base, size, flags, name);
    if (status != ZX_OK) {
        return status;
    }

    // Create a handle for the child
    return resource_out->make(fbl::move(child), rights);
}
