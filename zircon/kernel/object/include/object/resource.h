// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_RESOURCE_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_RESOURCE_H_

#include <zircon/compiler.h>
#include <zircon/syscalls/resource.h>
#include <zircon/types.h>

#include <fbl/ref_ptr.h>

// Resource constants (ZX_RSRC_KIND_..., etc) are located
// in system/public/zircon/syscalls/resource.h

// Determines if this handle is to a resource of the specified
// kind *or* to the root resource, which can stand in for any kind.
// Used to provide access to privileged syscalls.
zx_status_t validate_resource(zx_handle_t handle, zx_rsrc_kind_t kind);

// Validates a resource based on type and low/high range.
class ResourceDispatcher;
zx_status_t validate_ranged_resource(fbl::RefPtr<ResourceDispatcher> resource, zx_rsrc_kind_t kind,
                                     uint64_t base, size_t len);
zx_status_t validate_ranged_resource(zx_handle_t handle, zx_rsrc_kind_t kind, uint64_t base,
                                     size_t len);

// Validates enabling ioport access bits for a given process based on a resource handle
static inline zx_status_t validate_resource_ioport(zx_handle_t handle, uint64_t base, size_t len) {
  return validate_ranged_resource(handle, ZX_RSRC_KIND_IOPORT, base, len);
}

// Validates mapping an MMIO range based on a resource handle
static inline zx_status_t validate_resource_mmio(zx_handle_t handle, uint64_t base, size_t len) {
  return validate_ranged_resource(handle, ZX_RSRC_KIND_MMIO, base, len);
}

// Validates creation of an interrupt object based on a resource handle
static inline zx_status_t validate_resource_irq(zx_handle_t handle, uint32_t irq) {
  return validate_ranged_resource(handle, ZX_RSRC_KIND_IRQ, irq, 1);
}

// Validates access to a SMC service call number based on a resource handle
static inline zx_status_t validate_resource_smc(zx_handle_t handle, uint64_t service_call_num) {
  return validate_ranged_resource(handle, ZX_RSRC_KIND_SMC, service_call_num, 1);
}

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_RESOURCE_H_
