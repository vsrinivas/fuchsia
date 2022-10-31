// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_MESON_AML_MESON_PLL_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_MESON_AML_MESON_PLL_H_

#include <lib/mmio/mmio-view.h>
#include <lib/stdcompat/span.h>
#include <zircon/types.h>

#include <fbl/macros.h>
#include <soc/aml-meson/aml-pll.h>

namespace amlogic_clock {

class AmlMesonPllDevice {
 public:
  DISALLOW_COPY_ASSIGN_AND_MOVE(AmlMesonPllDevice);

  // Return correct clock rate table for selected clock.
  virtual const hhi_pll_rate_t* GetRateTable() const = 0;

  // Return the count of the rate table for the clock.
  virtual size_t GetRateTableSize() = 0;

  // Enable the selected clock.
  virtual zx_status_t Enable() = 0;

  // Disable the selected clock.
  virtual void Disable() = 0;

  // Set the rate of the selected clock.
  virtual zx_status_t SetRate(const uint64_t hz) = 0;

 protected:
  friend class std::default_delete<AmlMesonPllDevice>;
  AmlMesonPllDevice() {}

  virtual ~AmlMesonPllDevice() = default;
};

// Load the default register parameter.
void LoadInitConfig(const fdf::MmioView view, const meson_clk_pll_data_t config);

// Find frequency in the rate table and return pointer to the entry.
zx_status_t FetchRateTable(uint64_t hz, const cpp20::span<const hhi_pll_rate_t> rates_table,
                           const hhi_pll_rate_t** pll_rate);

}  // namespace amlogic_clock
#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_MESON_AML_MESON_PLL_H_
