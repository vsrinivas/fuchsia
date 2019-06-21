// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <fbl/ref_ptr.h>
#include <kernel/range_check.h>
#include <object/process_dispatcher.h>
#include <object/resource_dispatcher.h>
#include <object/resource.h>
#include <zircon/syscalls/resource.h>
#include <trace.h>

#define LOCAL_TRACE 0

// TODO(ZX-2419): Take another look at validation and consider returning
// dispatchers or move validation into the parent dispatcher itself.

// Check if the resource referenced by |handle| is of kind |kind|, or ZX_RSRC_KIND_ROOT.
//
// Possible errors:
// ++ ZX_ERR_ACCESS_DENIED: |handle| is not the right |kind| of handle.
// ++ ZX_ERR_WRONG_TYPE: |handle| is not a valid handle.
zx_status_t validate_resource(zx_handle_t handle, uint32_t kind) {
    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<ResourceDispatcher> resource;
    auto status = up->GetDispatcher(handle, &resource);
    if (status != ZX_OK) {
        return status;
    }

    auto res_kind = resource->get_kind();
    if (res_kind == kind || res_kind == ZX_RSRC_KIND_ROOT) {
        return ZX_OK;
    }

    return ZX_ERR_WRONG_TYPE;
}

// Check if the resource referenced by |handle| is of kind |kind|, or ZX_RSRC_KIND_ROOT. If
// |kind| matches the resource's kind, then range validation between |base| and |size| will
// be made against the resource's backing address space allocation.
//
// Possible errors:
// ++ ZX_ERR_ACCESS_DENIED: |handle| is not a valid handle.
// ++ ZX_ERR_WRONG_TYPE: |handle| is not a valid Resource handle, or does not match |kind|.
// ++ ZX_ERR_OUT_OF_RANGE: The range specified by |base| and |Len| is not granted by this
// resource.
zx_status_t validate_ranged_resource(zx_handle_t handle,
                                     uint32_t kind,
                                     uintptr_t base,
                                     size_t size) {
    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<ResourceDispatcher> resource;
    auto status = up->GetDispatcher(handle, &resource);
    if (status != ZX_OK) {
        return status;
    }

    // Root gets access to everything and has no region to match against
    if (resource->get_kind() == ZX_RSRC_KIND_ROOT) {
        return ZX_OK;
    }

    if (resource->get_kind() != kind) {
        return ZX_ERR_WRONG_TYPE;
    }

    // TODO(cja): when more ranged types are added we will need to move this sort of adjustment to
    // specific validation methods.
    uint64_t rbase = resource->get_base();
    size_t rsize = resource->get_size();
    if (resource->get_kind() == ZX_RSRC_KIND_MMIO) {
        const uint64_t aligned_rbase = ROUNDDOWN(rbase, PAGE_SIZE);
        rsize = PAGE_ALIGN((rbase - aligned_rbase) + rsize);
        rbase = aligned_rbase;
    }
    LTRACEF("req [base %#lx size %#lx] and resource [base %#lx size %#lx]\n", base, size, rbase, rsize);

    // Check for intersection and make sure the requested base+size fits within
    // the resource's address space  allocation.
    uintptr_t ibase;
    size_t isize;
    if (!GetIntersect(base, size, rbase, rsize, &ibase, &isize) ||
            isize != size ||
            ibase != base) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    return ZX_OK;
}
