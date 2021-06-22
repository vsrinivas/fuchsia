// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/unwinder/unwind_local.h"

#include <link.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#include "src/developer/debug/unwinder/memory.h"
#include "src/developer/debug/unwinder/third_party/libunwindstack/context.h"

namespace unwinder {

std::vector<Frame> UnwindLocal() {
  struct Data {
    std::vector<uint64_t> modules;
    BoundedLocalMemory memory;
  } data;

  // Initialize the memory with proper start/end boundaries.
  using CallbackType = int (*)(dl_phdr_info * info, size_t size, void* modules);
  CallbackType dl_iterate_phdr_callback = [](dl_phdr_info* info, size_t, void* data_v) {
    auto data = reinterpret_cast<Data*>(data_v);
    data->modules.push_back(info->dlpi_addr);
    for (size_t i = 0; i < info->dlpi_phnum; i++) {
      const Elf64_Phdr& phdr = info->dlpi_phdr[i];
      if (phdr.p_type == PT_LOAD) {
        data->memory.AddRegion(info->dlpi_addr + phdr.p_vaddr, phdr.p_memsz);
      }
    }
    return 0;
  };
  dl_iterate_phdr(dl_iterate_phdr_callback, &data);

  std::map<uint64_t, Memory*> module_map;
  for (auto base : data.modules) {
    module_map.emplace(base, &data.memory);
  }

  LocalMemory stack;
  auto frames = Unwind(&stack, module_map, GetContext());

  if (frames.empty()) {
    return {};
  }
  // Drop the first frame.
  return {frames.begin() + 1, frames.end()};
}

}  // namespace unwinder
