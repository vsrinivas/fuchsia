// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ROOT_RESOURCE_FILTER_INCLUDE_LIB_ROOT_RESOURCE_FILTER_H_
#define ZIRCON_KERNEL_LIB_ROOT_RESOURCE_FILTER_INCLUDE_LIB_ROOT_RESOURCE_FILTER_H_

#include <stddef.h>
#include <stdint.h>
#include <zircon/syscalls/resource.h>

// Called by platform specific code to add a range to a specific resource type's
// deny list.  Must be called after global .ctors, heap initialization, and
// after blocking is permitted.  Once added to the deny list, resource ranges
// which intersect any of the denied ranges may not be created, even with the
// root resource.  This is primarily used to ensure that even user-mode code may
// not gain direct access to RAM, or to other kernel exclusive resources such as
// the interrupt controller or IOMMU.
void root_resource_filter_add_deny_region(uintptr_t base, size_t size, zx_rsrc_kind_t kind);

// Called by object/resource.cc code to check whether or not a resource of the
// specified range and kind may be created.  This restriction applies even to
// users with access to the root resource.
bool root_resource_filter_can_access_region(uintptr_t base, size_t size, zx_rsrc_kind_t kind);

#endif  // ZIRCON_KERNEL_LIB_ROOT_RESOURCE_FILTER_INCLUDE_LIB_ROOT_RESOURCE_FILTER_H_
