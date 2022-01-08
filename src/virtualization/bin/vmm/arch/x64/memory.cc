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

static constexpr uint64_t kAddr32kb     = 0x0000000000008000;
static constexpr uint64_t kAddr512kb    = 0x0000000000080000;
static constexpr uint64_t kAddr1mb      = 0x0000000000100000;

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
void Append(std::vector<Format>& ranges, size_t mem_size, const DevMem& dev_mem) {
  // 0 to 32kb is reserved.
  Append<Format>(ranges, 0, kAddr32kb, Type::kReserved);
  // 32kb to to 512kb is available (for Linux's real mode trampoline).
  Append<Format>(ranges, kAddr32kb, kAddr512kb - kAddr32kb, Type::kRam);
  // 512kb to 1mb is reserved.
  Append<Format>(ranges, kAddr512kb, kAddr1mb - kAddr512kb, Type::kReserved);

  if (mem_size > kAddr1mb) {
    // 1mb to mem_size is available.
    dev_mem.YieldInverseRange(kAddr1mb, mem_size - kAddr1mb,
                              [&ranges](zx_gpaddr_t addr, size_t size) {
                                Append<Format>(ranges, addr, size, Type::kRam);
                              });
  }

  for (const auto& range : dev_mem) {
    Append<Format>(ranges, range.addr, range.size, Type::kReserved);
  }
}

}  // namespace

E820Map::E820Map(size_t mem_size, const DevMem& dev_mem) {
  Append<E820Entry>(entries_, mem_size, dev_mem);
}

#ifdef __Fuchsia__

std::vector<zbi_mem_range_t> ZbiMemoryRanges(
    const std::vector<fuchsia::virtualization::MemorySpec>& specs, size_t mem_size,
    const DevMem& dev_mem) {
  std::vector<zbi_mem_range_t> ranges;
  Append<zbi_mem_range_t>(ranges, mem_size, dev_mem);
  return ranges;
}

#endif
