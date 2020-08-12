// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ELF_SEARCH_H_
#define ELF_SEARCH_H_

#include <elf.h>
#include <lib/zx/process.h>
#include <stdint.h>

#include <string_view>

#include <fbl/function.h>
#include <fbl/span.h>

namespace elf_search {

#if defined(__aarch64__)
constexpr Elf64_Half kNativeElfMachine = EM_AARCH64;
#elif defined(__x86_64__)
constexpr Elf64_Half kNativeElfMachine = EM_X86_64;
#endif

struct ModuleInfo {
  std::string_view name;
  uintptr_t vaddr;
  fbl::Span<const uint8_t> build_id;
  const Elf64_Ehdr& ehdr;
  fbl::Span<const Elf64_Phdr> phdrs;
};

using ModuleAction = fbl::Function<void(const ModuleInfo&)>;
extern zx_status_t ForEachModule(const zx::process&, ModuleAction);

}  // namespace elf_search

#endif  // ELF_SEARCH_H_
