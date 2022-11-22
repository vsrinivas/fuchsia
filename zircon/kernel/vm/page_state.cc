// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <vm/page_state.h>

const char* page_state_to_string(vm_page_state state) {
  switch (state) {
    case vm_page_state::FREE:
      return "free";
    case vm_page_state::ALLOC:
      return "alloc";
    case vm_page_state::WIRED:
      return "wired";
    case vm_page_state::HEAP:
      return "heap";
    case vm_page_state::OBJECT:
      return "object";
    case vm_page_state::MMU:
      return "mmu";
    case vm_page_state::IPC:
      return "ipc";
    case vm_page_state::CACHE:
      return "cache";
    case vm_page_state::SLAB:
      return "slab";
    case vm_page_state::ZRAM:
      return "zram";
    default:
      return "unknown";
  }
}
