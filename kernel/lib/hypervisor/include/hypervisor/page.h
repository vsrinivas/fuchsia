// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <vm/physmap.h>
#include <vm/pmm.h>

namespace hypervisor {

class Page {
public:
    Page() = default;
    DISALLOW_COPY_ASSIGN_AND_MOVE(Page);

    ~Page() {
      if (page_ != nullptr) {
          pmm_free_page(page_);
      }
    }

    zx_status_t Alloc(uint8_t fill) {
      zx_status_t status = pmm_alloc_page(0, &page_, &pa_);
      if (status != ZX_OK) {
          return status;
      }

      page_->state = VM_PAGE_STATE_WIRED;

      memset(VirtualAddress(), fill, PAGE_SIZE);
      return ZX_OK;
    }

    void* VirtualAddress() const {
      DEBUG_ASSERT(pa_ != 0);
      return paddr_to_physmap(pa_);
    }

    template <typename T>
    T* VirtualAddress() const {
        return static_cast<T*>(VirtualAddress());
    }

    paddr_t PhysicalAddress() const {
        DEBUG_ASSERT(pa_ != 0);
        return pa_;
    }

    bool IsAllocated() const { return pa_ != 0; }

private:
    vm_page* page_ = nullptr;
    zx_paddr_t pa_ = 0;
};

} // namespace hypervisor
