// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_DEV_IOMMU_INTEL_IOMMU_PAGE_H_
#define ZIRCON_KERNEL_DEV_IOMMU_INTEL_IOMMU_PAGE_H_

#include <stdint.h>
#include <zircon/types.h>

#include <fbl/macros.h>
#include <vm/pmm.h>

namespace intel_iommu {

static constexpr paddr_t kInvalidPaddr = UINT64_MAX;

// RAII object for managing the lifetime of the memory that backs hardware
// datastructures.
class IommuPage {
 public:
  IommuPage() : page_(nullptr), virt_(0) {}
  ~IommuPage();

  IommuPage(IommuPage&& p) : page_(p.page_), virt_(p.virt_) {
    p.page_ = nullptr;
    p.virt_ = 0;
  }
  IommuPage& operator=(IommuPage&& p) {
    page_ = p.page_;
    virt_ = p.virt_;
    p.page_ = nullptr;
    p.virt_ = 0;
    return *this;
  }

  static zx_status_t AllocatePage(IommuPage* out);

  uintptr_t vaddr() const { return virt_; }
  paddr_t paddr() const { return likely(page_) ? page_->paddr() : kInvalidPaddr; }

 private:
  IommuPage(vm_page_t* page, uintptr_t virt);
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(IommuPage);

  vm_page_t* page_;
  uintptr_t virt_;
};

}  // namespace intel_iommu

#endif  // ZIRCON_KERNEL_DEV_IOMMU_INTEL_IOMMU_PAGE_H_
