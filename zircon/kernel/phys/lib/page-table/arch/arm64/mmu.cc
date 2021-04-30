// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lib/page-table/arch/arm64/mmu.h"

#include <zircon/types.h>

namespace page_table::arm64 {

namespace {

// Get the number of bits represented by the given TcrTg0Value.
constexpr GranuleSize TcrTg0Bits(arch::ArmTcrTg0Value val) {
  switch (val) {
    case arch::ArmTcrTg0Value::k4KiB:
      return GranuleSize::k4KiB;
    case arch::ArmTcrTg0Value::k16KiB:
      return GranuleSize::k16KiB;
    case arch::ArmTcrTg0Value::k64KiB:
      return GranuleSize::k64KiB;
    default:
      ZX_PANIC("Invalid TG0 value.");
  }
}

}  // namespace

PageTableLayout PageTableLayout::FromTcrTtbr0(const arch::ArmTcrEl1& tcr) {
  return PageTableLayout{
      .granule_size = TcrTg0Bits(tcr.tg0()),
      .region_size_bits =
          static_cast<uint8_t>(64 - tcr.t0sz()),  // convert "bits ignored" to "bits used"
  };
}

GranuleSize GranuleForPageSize(PageSize page_size) {
  switch (page_size) {
      // 4 kiB granules
    case PageSize::k4KiB:
    case PageSize::k2MiB:
    case PageSize::k1GiB:
      return GranuleSize::k4KiB;

      // 16 kiB granules
    case PageSize::k16KiB:
    case PageSize::k32MiB:
      return GranuleSize::k16KiB;

      // 64 kiB granules
    case PageSize::k64KiB:
    case PageSize::k512MiB:
      return GranuleSize::k64KiB;
  }
}

}  // namespace page_table::arm64
