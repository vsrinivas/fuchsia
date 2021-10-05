// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_STATIC_PIE_RELOCATION_H_
#define ZIRCON_SYSTEM_ULIB_STATIC_PIE_RELOCATION_H_

#include <lib/static-pie/static-pie.h>
#include <lib/stdcompat/span.h>

#include <fbl/hard_int.h>

#include "elf-types.h"

// Exposed for testing.
namespace static_pie {

// Represents an ELF program mapped into memory at some offset.
class Program {
 public:
  explicit Program(cpp20::span<std::byte> program, LinkTimeAddr link_addr, RunTimeAddr load_addr)
      : base_(program.data()), link_addr_(link_addr), load_addr_(load_addr) {
    ZX_DEBUG_ASSERT(IsAlignedFor<uint64_t>(base_));
  }

  // Read a 64-bit word at the given offset in the program.
  uint64_t ReadWord(LinkTimeAddr address) const {
    ZX_DEBUG_ASSERT(IsAlignedFor<uint64_t>(base_ + (address - link_addr_)));
    return *reinterpret_cast<uint64_t*>(base_ + (address - link_addr_));
  }

  // Write a 64-bit word to the given offset in the program.
  void WriteWord(LinkTimeAddr address, uint64_t value) const {
    ZX_DEBUG_ASSERT(IsAlignedFor<uint64_t>(base_ + (address - link_addr_)));
    *reinterpret_cast<uint64_t*>(base_ + (address - link_addr_)) = value;
  }

  // Return an cpp20::span<T> to the given region of memory.
  template <typename T>
  cpp20::span<T> MapRegion(LinkTimeAddr address, size_t size) {
    ZX_DEBUG_ASSERT(IsAlignedFor<T>(base_ + (address - link_addr_)));
    return cpp20::span<T>(reinterpret_cast<T*>(base_ + (address - link_addr_)), size / sizeof(T));
  }

  // Link and load address of this program.
  LinkTimeAddr link_addr() const { return link_addr_; }
  RunTimeAddr load_addr() const { return load_addr_; }

  // Convert address types.
  RunTimeAddr ToRunTimeAddr(LinkTimeAddr addr) const { return load_addr_ + (addr - link_addr_); }

 private:
  // Return true if the pointer `addr` is correctly aligned for type `T`.
  template <typename T>
  static bool IsAlignedFor(void* addr) {
    return reinterpret_cast<uintptr_t>(addr) % alignof(T) == 0;
  }

  std::byte* base_;
  LinkTimeAddr link_addr_;
  RunTimeAddr load_addr_;
};

// Apply relocations in the given Rel/Rela/Relr table.
void ApplyRelaRelocs(const Program& program, cpp20::span<const Elf64RelaEntry> table);
void ApplyRelRelocs(const Program& program, cpp20::span<const Elf64RelEntry> table);
void ApplyRelrRelocs(const Program& program, cpp20::span<const uint64_t> table);

// Apply the relocations specified in the given ".dynamic" table.
void ApplyDynamicRelocs(Program& program, cpp20::span<const Elf64DynamicEntry> table);

}  // namespace static_pie

#endif  // ZIRCON_SYSTEM_ULIB_STATIC_PIE_RELOCATION_H_
