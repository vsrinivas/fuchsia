// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_MSI_ALLOCATION_DISPATCHER_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_MSI_ALLOCATION_DISPATCHER_H_

#include <lib/zircon-internal/thread_annotations.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/rights.h>
#include <zircon/types.h>

#include <fbl/canary.h>
#include <fbl/ref_ptr.h>
#include <object/dispatcher.h>
#include <object/msi_allocation.h>

class MsiAllocationDispatcher final
    : public SoloDispatcher<MsiAllocationDispatcher, ZX_DEFAULT_MSI_RIGHTS> {
 public:
  static zx_status_t Create(fbl::RefPtr<MsiAllocation> msi_alloc,
                            KernelHandle<MsiAllocationDispatcher>* handle, zx_rights_t* rights) {
    fbl::AllocChecker ac;
    KernelHandle new_handle(fbl::AdoptRef(new (&ac) MsiAllocationDispatcher(ktl::move(msi_alloc))));
    if (!ac.check()) {
      return ZX_ERR_NO_MEMORY;
    }

    *rights = default_rights();
    *handle = ktl::move(new_handle);
    return ZX_OK;
  }

  zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_MSI_ALLOCATION; }
  void GetInfo(zx_info_msi_t* info) { msi_alloc_->GetInfo(info); }
  fbl::RefPtr<MsiAllocation>& msi_allocation() { return msi_alloc_; }

 private:
  explicit MsiAllocationDispatcher(fbl::RefPtr<MsiAllocation>&& msi_alloc)
      : msi_alloc_(ktl::move(msi_alloc)) {}
  fbl::RefPtr<MsiAllocation> msi_alloc_;
};
#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_MSI_ALLOCATION_DISPATCHER_H_
