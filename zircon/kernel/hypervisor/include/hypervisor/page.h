// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_PAGE_H_
#define ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_PAGE_H_

#include <lib/zx/result.h>

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

  zx::result<> Alloc(uint8_t fill) {
    zx_status_t status = pmm_alloc_page(0, &page_, &pa_);
    if (status != ZX_OK) {
      return zx::error(status);
    }

    page_->set_state(vm_page_state::WIRED);
    memset(VirtualAddress(), fill, PAGE_SIZE);
    return zx::ok();
  }

  void* VirtualAddress() const {
    DEBUG_ASSERT(pa_ != 0);
    return paddr_to_physmap(pa_);
  }

  template <typename T>
  T* VirtualAddress() const {
    return static_cast<T*>(VirtualAddress());
  }

  zx_paddr_t PhysicalAddress() const {
    DEBUG_ASSERT(pa_ != 0);
    return pa_;
  }

  bool IsAllocated() const { return pa_ != 0; }

 private:
  vm_page* page_ = nullptr;
  zx_paddr_t pa_ = 0;
};

template <typename T>
class PagePtr {
 public:
  zx::result<> Alloc() {
    auto result = page_.Alloc(0);
    if (result.is_error()) {
      return result.take_error();
    }

    ptr_ = page_.VirtualAddress<T>();
    new (ptr_) T;
    return zx::ok();
  }

  zx_paddr_t PhysicalAddress() const { return page_.PhysicalAddress(); }
  T* get() const { return ptr_; }
  T* operator->() const { return ptr_; }

 private:
  Page page_;
  T* ptr_;
};

}  // namespace hypervisor

#endif  // ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_PAGE_H_
