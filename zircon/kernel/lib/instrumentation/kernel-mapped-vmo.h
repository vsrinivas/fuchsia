// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_INSTRUMENTATION_KERNEL_MAPPED_VMO_H_
#define ZIRCON_KERNEL_LIB_INSTRUMENTATION_KERNEL_MAPPED_VMO_H_

#include <ktl/string_view.h>
#include <vm/pinned_vm_object.h>
#include <vm/vm_address_region.h>

class Handle;

// This maintains a kernel mapping from a VMO; mapped pages must stay pinned to
// prevent kernel-mode page faults.  Destroying this object unmaps and unpins.
class KernelMappedVmo {
 public:
  ~KernelMappedVmo();

  // Initialize with a kernel mapping from the VMO.
  zx_status_t Init(fbl::RefPtr<VmObject> vmo, size_t offset, size_t size, const char* name);

  // Publish this VMO to userland as a read-only handle using the given name
  // and content size.
  Handle* Publish(ktl::string_view vmo_name, size_t content_size);

  // Return the bounds of the mapping in the kernel address space.
  vaddr_t base() const { return mapping_->base(); }
  size_t size() const { return mapping_->size(); }

 private:
  PinnedVmObject pinned_vmo_;
  fbl::RefPtr<VmMapping> mapping_;
};

#endif  // ZIRCON_KERNEL_LIB_INSTRUMENTATION_KERNEL_MAPPED_VMO_H_
