// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_INCLUDE_LIB_MEMALLOC_RANGE_H_
#define ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_INCLUDE_LIB_MEMALLOC_RANGE_H_

#include <lib/fit/function.h>
#include <lib/stdcompat/span.h>
#include <zircon/boot/image.h>

#include <string_view>

namespace memalloc {

// The type of a physical memory rande. Represented by 64 bits, the bottom half
// of its value space are reserved for memory range types defined in the ZBI
// spec.
enum class Type : uint64_t {
  kFreeRam = ZBI_MEM_RANGE_RAM,
  kPeripheral = ZBI_MEM_RANGE_PERIPHERAL,
  kReserved = ZBI_MEM_RANGE_RESERVED,

  // TODO(fxbug.dev/77359): Add custom types above 0xffff'ffff.
};

std::string_view ToString(Type type);

// A memory range type that is layout-compatible to zbi_mem_range_t, but with
// the benefit of being able to use custom types.
struct MemRange {
  uint64_t addr;
  uint64_t size;
  Type type;

  constexpr bool operator==(const MemRange& other) const {
    return addr == other.addr && size == other.size && type == other.type;
  }
  constexpr bool operator!=(const MemRange& other) const { return !(*this == other); }

  // Gives a lexicographic order on MemRange.
  constexpr bool operator<(const MemRange& other) const {
    return addr < other.addr || (addr == other.addr && size < other.size);
  }
};

// TODO(joshuaseaton): static_assert(cpp20::is_layout_compatible_v<MemRange, zbi_mem_range_t>);

}  // namespace memalloc

#endif  // ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_INCLUDE_LIB_MEMALLOC_RANGE_H_
