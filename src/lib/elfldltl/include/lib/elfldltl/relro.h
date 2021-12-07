// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_RELRO_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_RELRO_H_

#include <lib/stdcompat/span.h>

#include <optional>
#include <utility>

#include "layout.h"

namespace elfldltl {

// Returns {start, size} whole-page subregion of the segment, possibly {0, 0}.
template <class Phdr>
constexpr auto RelroBounds(const Phdr& phdr, decltype(phdr.memsz()) pagesize)
    -> std::pair<decltype(phdr.vaddr()), decltype(phdr.memsz())> {
  auto start = (phdr.vaddr + pagesize - 1) & -pagesize;
  auto end = phdr.vaddr + phdr.memsz;
  if (start >= end) {
    return {};
  }
  end &= -pagesize;
  if (start >= end) {
    return {};
  }
  return {start, end - start};
}

// Given a span of all the phdrs, find and reduce the PT_GNU_RELRO segment.
template <class Phdr>
constexpr auto RelroBounds(cpp20::span<const Phdr> phdrs, decltype(phdrs[0].memsz()) pagesize)
    -> std::pair<decltype(phdrs[0].vaddr()), decltype(phdrs[0].memsz())> {
  for (const Phdr& phdr : phdrs) {
    if (phdr.type == elfldltl::ElfPhdrType::kRelro) {
      return RelroBounds(phdr, pagesize);
    }
  }
  return {};
}

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_RELRO_H_
