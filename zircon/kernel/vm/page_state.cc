// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <vm/page_state.h>

const char* page_state_to_string(vm_page_state state) {
  switch (state) {
    case VM_PAGE_STATE_FREE:
      return "free";
    case VM_PAGE_STATE_ALLOC:
      return "alloc";
    case VM_PAGE_STATE_WIRED:
      return "wired";
    case VM_PAGE_STATE_HEAP:
      return "heap";
    case VM_PAGE_STATE_OBJECT:
      return "object";
    case VM_PAGE_STATE_MMU:
      return "mmu";
    case VM_PAGE_STATE_IPC:
      return "ipc";
    case VM_PAGE_STATE_CACHE:
      return "cache";
    case VM_PAGE_STATE_SLAB:
      return "slab";
    default:
      return "unknown";
  }
}
