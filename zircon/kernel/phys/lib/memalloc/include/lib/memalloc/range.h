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

#include <array>
#include <string_view>

namespace memalloc {

constexpr uint64_t kMinExtendedTypeValue =
    static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) + 1;

// The type of a physical memory range. Represented by 64 bits, the lower 2^32
// values in the space are reserved for memory range types defined in the ZBI
// spec, the "base types"; the types in the upper half are referred to as
// "extended types", and increment from kMinExtendedTypeValue in value.
//
// As is detailed in the ZBI spec regarding overlaps, among the base types,
// kReserved and kPeripheral ranges have the highest precedence, in that order.
// Further, by definition here, an extended type is only permitted to overlap
// with kFreeRam or the same type.
enum class Type : uint64_t {
  //
  // ZBI memory range types:
  //

  kFreeRam = ZBI_MEM_RANGE_RAM,
  kPeripheral = ZBI_MEM_RANGE_PERIPHERAL,
  kReserved = ZBI_MEM_RANGE_RESERVED,

  //
  // Extended types:
  //

  // Reserved for internal bookkeeping.
  kPoolBookkeeping = kMinExtendedTypeValue,

  // The phys kernel memory image.
  kPhysKernel,

  // The data ZBI, as placed by the bootloader.
  kDataZbi,

  // TODO(fxbug.dev/77359): define more...

  // A placeholder value signifying the last extended type. It must not be used
  // as an actual type value.
  kMaxExtended,
};

static_assert(static_cast<uint64_t>(Type::kFreeRam) < kMinExtendedTypeValue);
static_assert(static_cast<uint64_t>(Type::kPeripheral) < kMinExtendedTypeValue);
static_assert(static_cast<uint64_t>(Type::kReserved) < kMinExtendedTypeValue);

constexpr uint64_t kMaxExtendedTypeValue = static_cast<uint64_t>(Type::kMaxExtended);
constexpr size_t kNumExtendedTypes = kMaxExtendedTypeValue - kMinExtendedTypeValue;
constexpr size_t kNumBaseTypes = 3;

std::string_view ToString(Type type);

// A memory range type that is layout-compatible to zbi_mem_range_t, but with
// the benefit of being able to use extended types.
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

namespace internal {

// Effectively just a span and an iterator. This is used internally to iterate
// over a variable number of range arrays.
struct MemRangeIterationContext {
  MemRangeIterationContext() = default;

  // Lexicographically sorts the ranges on construction.
  explicit MemRangeIterationContext(cpp20::span<MemRange> ranges)
      : ranges_(ranges), it_(ranges.begin()) {
    std::sort(ranges_.begin(), ranges_.end());
  }

  cpp20::span<MemRange> ranges_;
  typename cpp20::span<MemRange>::iterator it_;
};

}  // namespace internal

}  // namespace memalloc

#endif  // ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_INCLUDE_LIB_MEMALLOC_RANGE_H_
