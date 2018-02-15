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
      vm_page_t* page = paddr_to_vm_page(pa_);
      if (page != nullptr)
          pmm_free_page(page);
    }

    zx_status_t Alloc(uint8_t fill) {
      vm_page_t* page = pmm_alloc_page(0, &pa_);
      if (page == nullptr)
          return ZX_ERR_NO_MEMORY;

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
    zx_paddr_t pa_ = 0;
};

} // namespace hypervisor
