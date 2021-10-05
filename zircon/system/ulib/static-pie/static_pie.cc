// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/static-pie/static-pie.h>

#include "relocation.h"

namespace static_pie {

// Beginning of the ELF ".dynamic" section.
extern "C" __LOCAL const Elf64DynamicEntry _DYNAMIC[];

void ApplyDynamicRelocationsToSelf(uintptr_t link_address, uintptr_t load_address) {
  Program program(cpp20::span(reinterpret_cast<std::byte*>(load_address), SIZE_MAX),
                  LinkTimeAddr(link_address), RunTimeAddr(load_address));

  // Apply relocations.
  ApplyDynamicRelocs(program, cpp20::span<const Elf64DynamicEntry>(_DYNAMIC, SIZE_MAX));

  // Compiler barrier. Ensure stores are committed prior to return.
  std::atomic_signal_fence(std::memory_order_seq_cst);
}

}  // namespace static_pie
