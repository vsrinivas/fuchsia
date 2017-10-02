// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/resources.h>

#include <fbl/ref_ptr.h>
#include <object/process_dispatcher.h>
#include <object/resource_dispatcher.h>
#include <zircon/syscalls/resource.h>

zx_status_t validate_resource(zx_handle_t handle, uint32_t kind) {
    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<ResourceDispatcher> resource;
    auto status = up->GetDispatcher(handle, &resource);
    if (status != ZX_OK) {
        return status;
    }
    uint32_t rkind = resource->get_kind();
    if ((rkind == ZX_RSRC_KIND_ROOT) || (rkind == kind)) {
        return ZX_OK;
    }
    return ZX_ERR_ACCESS_DENIED;
}

zx_status_t validate_ranged_resource(zx_handle_t handle, uint32_t kind, uint64_t low,
                                     uint64_t high) {
    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<ResourceDispatcher> resource;
    auto status = up->GetDispatcher(handle, &resource);
    if (status != ZX_OK) {
        return status;
    }
    uint32_t rsrc_kind = resource->get_kind();
    if (rsrc_kind == ZX_RSRC_KIND_ROOT) {
        // root resource is valid for everything
        return ZX_OK;
    } else if (rsrc_kind == kind) {
        uint64_t rsrc_low, rsrc_high;
        resource->get_range(&rsrc_low, &rsrc_high);
        if (low >= rsrc_low && high <= rsrc_high) {
            return ZX_OK;
        }
    }

    return ZX_ERR_ACCESS_DENIED;
}
