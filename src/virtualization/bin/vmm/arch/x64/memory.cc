// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zircon-internal/e820.h>
#include <zircon/boot/image.h>

#include "src/virtualization/bin/vmm/arch/x64/e820.h"

#ifdef __Fuchsia__
#include <fuchsia/virtualization/cpp/fidl.h>

#include "src/virtualization/bin/vmm/memory.h"
#endif

namespace {

// clang-format off

constexpr uint64_t kAddr32kb     = 0x0000000000008000;
constexpr uint64_t kAddr512kb    = 0x0000000000080000;
constexpr uint64_t kAddr1mb      = 0x0000000000100000;

// clang-format on

enum class Type { kRam, kReserved };

template <typename Format>
void Append(std::vector<Format>& ranges, uint64_t addr, uint64_t size, Type type) {
  if constexpr (std::is_same_v<Format, zbi_mem_range_t>) {
    ranges.emplace_back(zbi_mem_range_t{
        .paddr = addr,
        .length = size,
        .type =
            static_cast<uint32_t>(type == Type::kRam ? ZBI_MEM_RANGE_RAM : ZBI_MEM_RANGE_RESERVED),
    });
  } else {
    static_assert(std::is_same_v<Format, E820Entry>, "unrecognized memory format");
    ranges.emplace_back(E820Entry{
        .addr = addr,
        .size = size,
        .type = type == Type::kRam ? E820Type::kRam : E820Type::kReserved,
    });
  }
}

template <typename Format>
void AppendSpecialRegions(std::vector<Format>& ranges) {
  // 0 to 32kb is reserved.
  Append<Format>(ranges, 0, kAddr32kb, Type::kReserved);
  // 512kb to 1mb is reserved.
  Append<Format>(ranges, kAddr512kb, kAddr1mb - kAddr512kb, Type::kReserved);
}

template <typename Format>
void Append(std::vector<Format>& ranges, const DevMem& dev_mem,
            const std::vector<GuestMemoryRegion>& guest_mem) {
  // The first 1 MiB has special reserved regions for x86, although we do not treat them as
  // regular device memory and trap on them. These reserved ranges are used by the guest BIOS.
  AppendSpecialRegions(ranges);

  for (const GuestMemoryRegion& mem : guest_mem) {
    Append<Format>(ranges, mem.base, mem.size, Type::kRam);
  }

  for (const auto& range : dev_mem) {
    Append<Format>(ranges, range.addr, range.size, Type::kReserved);
  }
}

}  // namespace

E820Map::E820Map(const DevMem& dev_mem, const std::vector<GuestMemoryRegion>& guest_mem) {
  Append<E820Entry>(entries_, dev_mem, guest_mem);
}

#ifdef __Fuchsia__

std::vector<zbi_mem_range_t> ZbiMemoryRanges(const DevMem& dev_mem,
                                             const std::vector<GuestMemoryRegion>& guest_mem) {
  std::vector<zbi_mem_range_t> ranges;
  Append<zbi_mem_range_t>(ranges, dev_mem, guest_mem);
  return ranges;
}

#endif
