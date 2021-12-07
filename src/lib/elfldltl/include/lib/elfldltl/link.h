// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_LINK_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_LINK_H_

// This file provides template APIs that do the central orchestration of
// dynamic linking: resolving and applying relocations.

#include "relocation.h"

namespace elfldltl {

// Apply simple fixups as directed by Elf::RelocationInfo, given the load bias:
// the difference between runtime addresses and addresses that appear in the
// relocation records.  This calls memory.Store(reloc_address, runtime_address)
// or memory.StoreAdd(reloc_address, bias) to store the adjusted values.
// Returns false iff any calls into the Memory object returned false.
template <class Memory, class RelocInfo>
[[nodiscard]] constexpr bool RelocateRelative(Memory& memory, const RelocInfo& info,
                                              typename RelocInfo::size_type bias) {
  using Addr = typename RelocInfo::Addr;
  using size_type = typename RelocInfo::size_type;

  struct Visitor {
    // RELA entry with separate addend.
    constexpr bool operator()(const typename RelocInfo::Rela& reloc) const {
      auto addr = bias_ + reloc.addend();
      return memory_.template Store<Addr>(reloc.offset, addr);
    }

    // REL or RELR entry with addend in place.
    constexpr bool operator()(size_type addr) const {
      return memory_.template StoreAdd<Addr>(addr, bias_);
    }

    Memory& memory_;
    size_type bias_;
  };

  return info.VisitRelative(Visitor{memory, bias});
}

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_LINK_H_
