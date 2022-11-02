// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/memalloc/range.h>
#include <lib/stdcompat/span.h>
#include <zircon/boot/image.h>

#include <string_view>
#include <type_traits>

namespace memalloc {

using namespace std::string_view_literals;

std::string_view ToString(Type type) {
  using namespace std::string_view_literals;

  switch (type) {
    case Type::kFreeRam:
      return "free RAM"sv;
    case Type::kReserved:
      return "reserved"sv;
    case Type::kPeripheral:
      return "peripheral"sv;
    case Type::kPoolBookkeeping:
      return "bookkeeping"sv;
    case Type::kPhysKernel:
      return "phys ZBI kernel image"sv;
    case Type::kPhysElf:
      return "phys ELF image"sv;
    case Type::kKernel:
      return "kernel image"sv;
    case Type::kFixedAddressKernel:
      return "fixed-address kernel image"sv;
    case Type::kKernelStorage:
      return "decompressed kernel payload"sv;
    case Type::kDataZbi:
      return "data ZBI"sv;
    case Type::kLegacyBootData:
      return "legacy boot data";
    case Type::kIdentityPageTables:
      return "identity page tables"sv;
    case Type::kPhysScratch:
      return "phys scratch"sv;
    case Type::kPoolTestPayload:
      return "memalloc::Pool test payload"sv;
    case Type::kZbiTestPayload:
      return "ZBI test payload"sv;
    case Type::kTestRamReserve:
      return "kernel.test.ram.reserve"sv;
    case Type::kNvram:
      return "ZBI_TYPE_NVRAM"sv;
    case Type::kMaxExtended:
      return "kMaxExtended"sv;
  }
  return "unknown"sv;
}

cpp20::span<Range> AsRanges(cpp20::span<zbi_mem_range_t> ranges) {
  static_assert(std::is_standard_layout_v<Range>);
  static_assert(std::is_standard_layout_v<zbi_mem_range_t>);
  static_assert(offsetof(Range, addr) == offsetof(zbi_mem_range_t, paddr));
  static_assert(offsetof(Range, size) == offsetof(zbi_mem_range_t, length));
  static_assert(offsetof(Range, type) == offsetof(zbi_mem_range_t, type));

  for (zbi_mem_range_t& range : ranges) {
    range.reserved = 0;
  }
  return {reinterpret_cast<Range*>(ranges.data()), ranges.size()};
}

}  // namespace memalloc
