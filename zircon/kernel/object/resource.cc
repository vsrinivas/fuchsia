// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "object/resource.h"

#include <align.h>
#include <lib/root_resource_filter.h>
#include <trace.h>
#include <zircon/syscalls/resource.h>

#include <fbl/ref_ptr.h>
#include <kernel/range_check.h>
#include <object/process_dispatcher.h>
#include <object/resource_dispatcher.h>

#define LOCAL_TRACE 0

// TODO(fxbug.dev/32272): Take another look at validation and consider returning
// dispatchers or move validation into the parent dispatcher itself.

// Check if the resource referenced by |handle| is of kind |kind|, or ZX_RSRC_KIND_ROOT.
//
// Possible errors:
// ++ ZX_ERR_ACCESS_DENIED: |handle| is not the right |kind| of handle.
// ++ ZX_ERR_WRONG_TYPE: |handle| is not a valid handle.
zx_status_t validate_resource(zx_handle_t handle, zx_rsrc_kind_t kind) {
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

zx_status_t validate_ranged_resource(fbl::RefPtr<ResourceDispatcher> resource, zx_rsrc_kind_t kind,
                                     uintptr_t base, size_t size) {
  // Root gets access to almost everything, but there are still resource ranges
  // it is not permitted to mint. For example:
  //
  // 1) All of physical RAM is off limits (with limited platform specific
  //    exceptions). It exists on the CPU accessible physical bus (so, the
  //    domain controlled by ZX_RSRC_KIND_MMIO) and user mode program should not
  //    be able to request access to physical RAM by address, they should be
  //    forced to go through the PMM using VMO creation instead.
  // 2) Any MMIO accessible interrupt controller registers.
  // 3) Any MMIO accessible IOMMU registers.
  //
  // Enforce that policy here by disallowing resource minting for any request
  // which touches any disallowed ranges.
  //
  if (resource->get_kind() == ZX_RSRC_KIND_ROOT) {
    if (!root_resource_filter_can_access_region(base, size, kind)) {
      return ZX_ERR_ACCESS_DENIED;
    }
    return ZX_OK;
  }

  if (resource->get_kind() != kind) {
    return ZX_ERR_WRONG_TYPE;
  }

  uint64_t rbase = resource->get_base();
  size_t rsize = resource->get_size();
  // In the specific case of MMIO, everything is rounded to PAGE_SIZE units
  // because it's the smallest unit we can operate at with the MMU.
  if (resource->get_kind() == ZX_RSRC_KIND_MMIO) {
    const uint64_t aligned_rbase = ROUNDDOWN(rbase, PAGE_SIZE);
    rsize = PAGE_ALIGN((rbase - aligned_rbase) + rsize);
    rbase = aligned_rbase;
  }
  LTRACEF("req [base %#lx size %#lx] and resource [base %#lx size %#lx]\n", base, size, rbase,
          rsize);

  // All resources need to track their lineage back to the root resource,
  // and the root resource is specifically prohibited from producing ranges
  // which intersect anything in the deny list. Since all resource ranges
  // need to be a subset of their parent, it should be impossible for a
  // resource object to exist with a range which intersects anything in the
  // deny list. Check that with a debug assert here.
  ZX_DEBUG_ASSERT(root_resource_filter_can_access_region(rbase, rsize, kind));

  // Check for intersection and make sure the requested base+size fits within
  // the resource's address space  allocation.
  uintptr_t ibase;
  size_t isize;
  if (!GetIntersect(base, size, rbase, rsize, &ibase, &isize) || isize != size || ibase != base) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  return ZX_OK;
}

// Check if the resource referenced by |handle| is of kind |kind|, or ZX_RSRC_KIND_ROOT. If
// |kind| matches the resource's kind, then range validation between |base| and |size| will
// be made against the resource's backing address space allocation.
//
// Possible errors:
// ++ ZX_ERR_ACCESS_DENIED: |handle| is not a valid handle.
// ++ ZX_ERR_WRONG_TYPE: |handle| is not a valid resource handle, or |kind| is invalid for
//                       the request.
// ++ ZX_ERR_OUT_OF_RANGE: The range specified by |base| and |Len| is not granted by this
// resource.
zx_status_t validate_ranged_resource(zx_handle_t handle, zx_rsrc_kind_t kind, uintptr_t base,
                                     size_t size) {
  auto up = ProcessDispatcher::GetCurrent();
  fbl::RefPtr<ResourceDispatcher> resource;
  auto status = up->GetDispatcher(handle, &resource);
  if (status != ZX_OK) {
    return status;
  }

  return validate_ranged_resource(resource, kind, base, size);
}
