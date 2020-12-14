// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_STATIC_PIE_RELOCATION_H_
#define ZIRCON_SYSTEM_ULIB_STATIC_PIE_RELOCATION_H_

#include <lib/static-pie/static-pie.h>

#include <fbl/span.h>

#include "elf-types.h"

// Exposed for testing.
namespace static_pie {

// Represents an ELF program mapped into memory at some offset.
class Program {
 public:
  explicit Program(fbl::Span<std::byte> program) : base_(program.data()) {
    ZX_DEBUG_ASSERT(IsAlignedFor<uint64_t>(base_));
  }

  // Read a 64-bit word at the given offset in the program.
  uint64_t ReadWord(uint64_t address) const {
    ZX_DEBUG_ASSERT(IsAlignedFor<uint64_t>(base_ + address));
    return *reinterpret_cast<uint64_t*>(base_ + address);
  }

  // Write a 64-bit word to the given offset in the program.
  void WriteWord(uint64_t address, uint64_t value) const {
    ZX_DEBUG_ASSERT(IsAlignedFor<uint64_t>(base_ + address));
    *reinterpret_cast<uint64_t*>(base_ + address) = value;
  }

  // Return an fbl::Span<T> to the given region of memory.
  template <typename T>
  fbl::Span<T> MapRegion(uint64_t address, size_t size) {
    ZX_DEBUG_ASSERT(IsAlignedFor<T>(base_ + address));
    return fbl::Span<T>(reinterpret_cast<T*>(base_ + address), size / sizeof(T));
  }

 private:
  // Return true if the pointer `addr` is correctly aligned for type `T`.
  template <typename T>
  static bool IsAlignedFor(void* addr) {
    return reinterpret_cast<uintptr_t>(addr) % alignof(T) == 0;
  }

  std::byte* base_;
};

// Apply relocations in the given Rel/Rela/Relr table.
void ApplyRelaRelocs(Program program, fbl::Span<const Elf64RelaEntry> table, uint64_t base);
void ApplyRelRelocs(Program program, fbl::Span<const Elf64RelEntry> table, uint64_t base);
void ApplyRelrRelocs(Program program, fbl::Span<const uint64_t> table, uint64_t base);

// Apply the relocations specified in the given ".dynamic" table.
void ApplyDynamicRelocs(Program program, fbl::Span<const Elf64DynamicEntry> table, uint64_t base);

}  // namespace static_pie

#endif  // ZIRCON_SYSTEM_ULIB_STATIC_PIE_RELOCATION_H_
