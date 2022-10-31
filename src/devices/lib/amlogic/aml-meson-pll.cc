// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ddk/debug.h>

#include <algorithm>
#include <iterator>

#include <soc/aml-meson/aml-meson-pll.h>

namespace amlogic_clock {

void LoadInitConfig(const fdf::MmioView view, const meson_clk_pll_data_t config) {
  for (uint32_t i = 0; i < config.init_count; i++) {
    view.Write32(config.init_regs[i].def, config.init_regs[i].reg_offset);
    if (config.init_regs[i].delay_us) {
      zx_nanosleep(zx_deadline_after(ZX_USEC(config.init_regs[i].delay_us)));
    }
  }
}

zx_status_t FetchRateTable(uint64_t hz, const cpp20::span<const hhi_pll_rate_t> rates_table,
                           const hhi_pll_rate_t** pll_rate) {
  for (uint32_t i = 0; i < rates_table.size(); i++) {
    if (hz == rates_table[i].rate) {
      *pll_rate = &rates_table[i];
      return ZX_OK;
    }
  }

  return ZX_ERR_NOT_FOUND;
}

}  // namespace amlogic_clock
