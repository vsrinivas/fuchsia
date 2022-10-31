// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A5_A5_HIU_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A5_A5_HIU_H_

#include <lib/ddk/debug.h>
#include <lib/mmio/mmio-buffer.h>
#include <lib/mmio/mmio-view.h>
#include <lib/stdcompat/span.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <fbl/macros.h>
#include <soc/aml-meson/aml-meson-pll.h>

namespace amlogic_clock::a5 {

typedef enum {
  SYS_PLL = 0,
  HIFI_PLL,
  MPLL0,
  MPLL1,
  MPLL2,
  MPLL3,
  PLL_COUNT,
} meson_plls_t;

class AmlA5SysPllDevice : public AmlMesonPllDevice {
 public:
  static std::unique_ptr<AmlMesonPllDevice> Create(
      const cpp20::span<const hhi_pll_rate_t> rates_table);

  // Initialize `AmlA5SysPllDevice`.
  zx_status_t Initialize() { return ZX_OK; }

  // Return correct clock rate table for selected clock.
  const hhi_pll_rate_t* GetRateTable() const final override { return rates_table_.data(); }

  // Return the count of the rate table for the clock.
  size_t GetRateTableSize() final override { return rates_table_.size(); }

  // Enable the selected clock.
  zx_status_t Enable() final override { return ZX_ERR_NOT_SUPPORTED; }

  // Disable the selected clock.
  void Disable() final override {}

  // Set the rate of the selected clock.
  zx_status_t SetRate(const uint64_t hz) final override { return ZX_ERR_NOT_SUPPORTED; }

 protected:
  AmlA5SysPllDevice(const cpp20::span<const hhi_pll_rate_t> rates_table)
      : rates_table_(rates_table) {}

 private:
  const cpp20::span<const hhi_pll_rate_t> rates_table_;
};

class AmlA5HifiPllDevice : public AmlMesonPllDevice {
 public:
  static std::unique_ptr<AmlMesonPllDevice> Create(
      fdf::MmioView view, const meson_clk_pll_data_t* data,
      const cpp20::span<const hhi_pll_rate_t> rates_table);

  // Initialize `AmlA5HifiPllDevice`.
  zx_status_t Initialize() {
    InitPll();
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
  AmlA5HifiPllDevice(fdf::MmioView view, const meson_clk_pll_data_t* data,
                     const cpp20::span<const hhi_pll_rate_t> rates_table)
      : view_(std::move(view)), data_(data), rates_table_(rates_table) {}

 private:
  // Initialize the selected pll.
  void InitPll();

  const fdf::MmioView view_;
  const meson_clk_pll_data_t* data_;
  const cpp20::span<const hhi_pll_rate_t> rates_table_;
};

class AmlA5MpllDevice : public AmlMesonPllDevice {
 public:
  static std::unique_ptr<AmlMesonPllDevice> Create(
      fdf::MmioView view, const meson_clk_pll_data_t* data,
      const cpp20::span<const hhi_pll_rate_t> rates_table);

  // Initialize `AmlA5HifiPllDevice`.
  zx_status_t Initialize() {
    InitPll();
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
  AmlA5MpllDevice(fdf::MmioView view, const meson_clk_pll_data_t* data,
                  const cpp20::span<const hhi_pll_rate_t> rates_table)
      : view_(std::move(view)), data_(data), rates_table_(rates_table) {}

 private:
  // Initialize the selected pll.
  void InitPll();

  const fdf::MmioView view_;
  const meson_clk_pll_data_t* data_;
  const cpp20::span<const hhi_pll_rate_t> rates_table_;
};

// Create the PLL device according to the specified ID.
std::unique_ptr<AmlMesonPllDevice> CreatePllDevice(fdf::MmioBuffer* mmio, const uint32_t pll_num);

}  // namespace amlogic_clock::a5
#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A5_A5_HIU_H_
