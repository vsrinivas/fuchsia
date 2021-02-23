// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_STATE_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_STATE_H_

#include <stddef.h>
#include <stdint.h>

// Defines the state of a VM page (|vm_page_t|).
//
// Be sure to keep this enum in sync with the definition of |vm_page_t|.
enum class vm_page_state : uint8_t {
  FREE = 0,
  ALLOC,
  OBJECT,
  WIRED,
  HEAP,
  MMU,    // allocated to serve arch-specific mmu purposes
  IOMMU,  // allocated for platform-specific iommu structures
  IPC,
  CACHE,
  SLAB,

  COUNT_
};

static inline constexpr size_t VmPageStateIndex(vm_page_state state) {
  return static_cast<size_t>(state);
}

typedef struct vm_page_counts {
  int64_t by_state[VmPageStateIndex(vm_page_state::COUNT_)];
} vm_page_counts_t;

// Returns a string description of |state|.
const char* page_state_to_string(vm_page_state state);

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_PAGE_STATE_H_
