// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/module_list.h"

#include <algorithm>

#include "src/developer/debug/debug_agent/process_handle.h"

namespace debug_ipc {

// Comparison function for checking for changes in the list.
bool operator==(const Module& a, const Module& b) {
  return a.base == b.base && a.name == b.name && a.debug_address == b.debug_address &&
         a.build_id == b.build_id;
}

}  // namespace debug_ipc

namespace debug_agent {

bool ModuleList::Update(const ProcessHandle& process, uint64_t dl_debug_addr) {
  ModuleVector new_ones = process.GetModules(dl_debug_addr);
  std::sort(new_ones.begin(), new_ones.end(), [](auto& a, auto& b) { return a.base < b.base; });

  if (modules_ == new_ones)
    return false;  // No change.

  modules_ = std::move(new_ones);
  return true;
}

}  // namespace debug_agent
