// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_STATIC_PIE_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_STATIC_PIE_H_

// This file implements self-relocation for a static PIE.  The instantiation of
// the templates here must be statically linked into the startup code of the
// PIE.  It must be called before anything that uses any relocated data,
// including implicit GOT or PLT references--i.e. anything not explicitly
// declared with [[gnu::visibility("hidden")]]--or initialized data containing
// pointer values.
//
// This supports only simple fixup, so the PIE cannot have any symbolic
// relocation records.  It need not even have a dynamic symbol table at all,
// only a .dynamic section.

#include <atomic>

#include "diagnostics.h"
#include "dynamic.h"
#include "link.h"
#include "memory.h"
#include "relocation.h"
#include "self.h"

namespace elfldltl {

// The templated form uses the elfldltl::Self implementation for the known
// ElfClass of the metadata, selected by the type of the first (self) argument.
// There are two optional std::byte* arguments for the bounds of the program
// image, as for elfldltl::Self::Memory(), which see.  Those arguments can be
// omitted for a normal PIE layout ELF runtime image where the `__ehdr_start`
// and `_end` link-time symbols define the bounds of the image.
template <class Self, class DiagnosticsType, typename... Args>
inline void LinkStaticPie(const Self& self, DiagnosticsType& diagnostics, Args&&... args) {
  using Elf = typename Self::Elf;
  using size_type = typename Elf::size_type;

  auto memory = Self::Memory(std::forward<Args>(args)...);
  auto bias = static_cast<size_type>(Self::LoadBias());

  RelocationInfo<Elf> reloc_info;
  DecodeDynamic(diagnostics, memory, Self::Dynamic(), DynamicRelocationInfoObserver(reloc_info));

  if (RelocateRelative(memory, reloc_info, bias)) {
    std::atomic_signal_fence(std::memory_order_seq_cst);
  } else [[unlikely]] {
    __builtin_trap();
  }
}

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_STATIC_PIE_H_
