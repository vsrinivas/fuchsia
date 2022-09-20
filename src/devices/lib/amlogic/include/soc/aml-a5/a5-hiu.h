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
#include <soc/aml-s905d2/s905d2-hiu.h>

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

namespace a5_pll {

typedef enum {
  SYS_PLL = 0,
  HIFI_PLL,
  MPLL0,
  MPLL1,
  MPLL2,
  MPLL3,
  PLL_COUNT,
} a5_plls_t;

struct reg_sequence {
  uint32_t reg_offset;
  uint32_t def;
  uint32_t delay_us;
};

typedef struct {
  const struct reg_sequence* init_regs;
  uint32_t init_count;
} meson_clk_pll_data_t;

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
std::unique_ptr<AmlMesonPllDevice> CreatePllDeviceA5(fdf::MmioBuffer* mmio, const uint32_t pll_num);

// Load the default register parameter.
void LoadInitConfig(const fdf::MmioView view, const meson_clk_pll_data_t config);

// Find frequency in the rate table and return pointer to the entry.
zx_status_t FetchRateTable(uint64_t hz, const cpp20::span<const hhi_pll_rate_t> rates_table,
                           const hhi_pll_rate_t** pll_rate);

}  // namespace a5_pll

}  // namespace amlogic_clock
#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_A5_A5_HIU_H_
