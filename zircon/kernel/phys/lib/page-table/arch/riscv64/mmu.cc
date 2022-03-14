// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lib/page-table/arch/riscv64/mmu.h"

#include <zircon/types.h>

namespace page_table::riscv64 {

GranuleSize GranuleForPageSize(PageSize page_size) {
  switch (page_size) {
      // 4 kiB granules
    case PageSize::k4KiB:
    case PageSize::k2MiB:
    case PageSize::k1GiB:
      return GranuleSize::k4KiB;
  }
}

}  // namespace page_table::riscv64
