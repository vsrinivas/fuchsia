// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A1_A1_HIU_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A1_A1_HIU_H_

#include <lib/ddk/debug.h>
#include <lib/mmio/mmio-buffer.h>
#include <lib/mmio/mmio-view.h>
#include <lib/stdcompat/span.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <fbl/macros.h>
#include <soc/aml-meson/aml-meson-pll.h>

namespace amlogic_clock::a1 {

typedef enum {
  SYS_PLL = 0,
  HIFI_PLL,
  PLL_COUNT,
} meson_plls_t;

// Note:
// For A1: od = 0, frac_reg_width = 19
//
//  frac_max = 1 << (frac_reg_width - 2)
//  out = [ 24M * (m + frac / frac_max) / n ] / ( 1 << od)
//
// e.g.
//    If want set hifi_pll to 1467'648'000 hz,
//  then we can get m/n = 1467.648M/24M ≈ 61.
//  Let m = 61, n = 1 firstly.
//
//  1. get fractional part.
//                        target_rate * N * frac_max
//    frac_cal = ROUND_UP(___________________________  -  M * frac_max );
//                                 24Mhz
//
//    then we can get frac_cal = 19923.
//
//  2. get final frac.
//
//    frac = min(frac_cal, (frac_max - 1))
//
//    frac = 19923.
//
class AmlA1PllDevice : public AmlMesonPllDevice {
 public:
  static std::unique_ptr<AmlMesonPllDevice> Create(
      fdf::MmioView view, const meson_clk_pll_data_t* data,
      const cpp20::span<const hhi_pll_rate_t> rates_table);

  // Initialize `AmlA1PllDevice`.
  zx_status_t Initialize() {
    // Set default rate.
    current_rate_ = rates_table_.end()->rate;
    return ZX_OK;
  }

  // Return correct clock rate table for selected clock.
  const hhi_pll_rate_t* GetRateTable() const final override { return rates_table_.data(); }

  // Return the count of the rate table for the clock.
  size_t GetRateTableSize() final override { return rates_table_.size(); }

  // Enable the selected clock.
  zx_status_t Enable() final override;

  // Disable the selected clock.
  void Disable() final override;

  // Set the rate of the selected clock.
  zx_status_t SetRate(const uint64_t hz) final override;

 protected:
  AmlA1PllDevice(fdf::MmioView view, const meson_clk_pll_data_t* data,
                 const cpp20::span<const hhi_pll_rate_t> rates_table)
      : view_(std::move(view)), data_(data), rates_table_(rates_table) {}

 private:
  const fdf::MmioView view_;
  const meson_clk_pll_data_t* data_;
  const cpp20::span<const hhi_pll_rate_t> rates_table_;
  uint64_t current_rate_ = 0;
};

// Create the PLL device according to the specified ID.
std::unique_ptr<AmlMesonPllDevice> CreatePllDevice(fdf::MmioBuffer* mmio, const uint32_t pll_num);

}  // namespace amlogic_clock::a1
#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A1_A1_HIU_H_
