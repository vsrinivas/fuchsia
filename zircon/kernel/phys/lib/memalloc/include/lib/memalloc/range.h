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

#include <algorithm>
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

  // The phys ZBI kernel memory image.
  kPhysKernel,

  // A phys ELF memory image.
  kPhysElf,

  // The kernel memory image.
  kKernel,

  // The kernel memory image at a fixed address of 1MiB.
  kFixedAddressKernel,

  // A (decompressed) STORAGE_KERNEL ZBI payload.
  kKernelStorage,

  // The data ZBI, as placed by the bootloader.
  kDataZbi,

  // Data structures related to legacy boot protocols.
  kLegacyBootData,

  // Identity-mapping page tables.
  kIdentityPageTables,

  // General scratch space used by the phys kernel, but that which is free for
  // the next kernel as of hand-off.
  kPhysScratch,

  // A generic allocated type for Pool tests.
  kPoolTestPayload,

  // A generic allocated type for ZBI tests.
  kZbiTestPayload,

  // Memory carved out for the kernel.test.ram.reserve boot option.
  kTestRamReserve,

  // Memory carved out for the ZBI_TYPE_NVRAM region.
  kNvram,

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

constexpr bool IsExtendedType(Type type) {
  return static_cast<uint64_t>(type) >= kMinExtendedTypeValue;
}

// A memory range type that is layout-compatible to zbi_mem_range_t, but with
// the benefit of being able to use extended types.
struct Range {
  uint64_t addr;
  uint64_t size;
  Type type;

  // The end of the memory range. This method may only be called if addr + size
  // has been normalized to not overflow.
  constexpr uint64_t end() const { return addr + size; }

  constexpr bool operator==(const Range& other) const {
    return addr == other.addr && size == other.size && type == other.type;
  }
  constexpr bool operator!=(const Range& other) const { return !(*this == other); }

  // Gives a lexicographic order on Range.
  constexpr bool operator<(const Range& other) const {
    return addr < other.addr || (addr == other.addr && size < other.size);
  }
};

// We have constrained Type so that the ZBI memory type's value space
// identically embeds into the lower 2^32 values of Type; the upper 2^32 values
// is reserved for Type's extensions. Accordingly, in order to coherently
// recast a zbi_mem_range_t as a Range, the former's `reserved` field -
// which, layout-wise, corresponds to the upper half of Type - must be zeroed
// out.
cpp20::span<Range> AsRanges(cpp20::span<zbi_mem_range_t> ranges);

namespace internal {

// Effectively just a span and an iterator. This is used internally to iterate
// over a variable number of range arrays.
struct RangeIterationContext {
  RangeIterationContext() = default;

  // Lexicographically sorts the ranges on construction.
  explicit RangeIterationContext(cpp20::span<Range> ranges) : ranges_(ranges), it_(ranges.begin()) {
    std::sort(ranges_.begin(), ranges_.end());
  }

  cpp20::span<Range> ranges_;
  typename cpp20::span<Range>::iterator it_;
};

}  // namespace internal

}  // namespace memalloc

#endif  // ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_INCLUDE_LIB_MEMALLOC_RANGE_H_
