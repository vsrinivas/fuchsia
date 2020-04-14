// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_CLOCK_DRIVERS_VS680_CLK_VS680_CLK_TYPES_H_
#define SRC_DEVICES_CLOCK_DRIVERS_VS680_CLK_VS680_CLK_TYPES_H_

#include <lib/mmio/mmio.h>
#include <lib/zx/time.h>
#include <zircon/types.h>

#include <soc/vs680/vs680-clk.h>

namespace clk {

// TODO(bradenkell): Use the clocktree library when it is finished.

class Vs680Clock {
 public:
  virtual zx_status_t Enable() const = 0;
  virtual zx_status_t Disable() const = 0;
  virtual zx_status_t IsEnabled(bool* is_enabled) const = 0;
  virtual zx_status_t SetRate(uint64_t hz) const = 0;
  virtual zx_status_t QuerySupportedRate(uint64_t hz, uint64_t* out_hz) const = 0;
  virtual zx_status_t GetRate(uint64_t* out_hz) const = 0;
  virtual zx_status_t SetInput(uint32_t idx) const = 0;
  virtual zx_status_t GetNumInputs(uint32_t* out_n) const = 0;
  virtual zx_status_t GetInput(uint32_t* out_index) const = 0;
};

class Vs680Pll : public Vs680Clock {
 public:
  Vs680Pll(ddk::MmioView pll_mmio, zx::duration reset_time)
      : pll_mmio_(pll_mmio), reset_time_(reset_time) {}
  ~Vs680Pll() {}

  zx_status_t SetRate(uint64_t hz) const override;
  zx_status_t QuerySupportedRate(uint64_t hz, uint64_t* out_hz) const override;
  zx_status_t GetRate(uint64_t* out_hz) const override;
  zx_status_t SetInput(uint32_t idx) const override;
  zx_status_t GetNumInputs(uint32_t* out_n) const override;
  zx_status_t GetInput(uint32_t* out_index) const override;

 protected:
  virtual void StartPllChange() const = 0;
  virtual void EndPllChange() const = 0;
  virtual uint64_t max_freq_hz() const = 0;

 private:
  const ddk::MmioView pll_mmio_;
  const zx::duration reset_time_;
};

class Vs680SysPll : public Vs680Pll {
 public:
  Vs680SysPll(ddk::MmioView pll_mmio, zx::duration reset_time, ddk::MmioView bypass_mmio,
              uint32_t bypass_bit)
      : Vs680Pll(pll_mmio, reset_time), bypass_mmio_(bypass_mmio), bypass_bit_(bypass_bit) {}
  ~Vs680SysPll() {}

  zx_status_t Enable() const override;
  zx_status_t Disable() const override;
  zx_status_t IsEnabled(bool* out_enabled) const override;
  zx_status_t GetRate(uint64_t* out_hz) const override;

 protected:
  void StartPllChange() const override;
  void EndPllChange() const override;
  uint64_t max_freq_hz() const override { return 1'200'000'000; }

 private:
  const ddk::MmioView bypass_mmio_;
  const uint32_t bypass_bit_;
};

class Vs680CpuPll : public Vs680SysPll {
 public:
  Vs680CpuPll(ddk::MmioView pll_mmio, zx::duration reset_time, ddk::MmioView bypass_mmio,
              uint32_t bypass_bit)
      : Vs680SysPll(pll_mmio, reset_time, bypass_mmio, bypass_bit) {}
  ~Vs680CpuPll() {}

 protected:
  uint64_t max_freq_hz() const override { return 2'200'000'000; }
};

class Vs680AVPll : public Vs680Pll {
 public:
  Vs680AVPll(ddk::MmioView pll_mmio, zx::duration reset_time, ddk::MmioView disable_mmio,
             uint32_t disable_bit)
      : Vs680Pll(pll_mmio, reset_time), disable_mmio_(disable_mmio), disable_bit_(disable_bit) {}
  ~Vs680AVPll() {}

  zx_status_t Enable() const override;
  zx_status_t Disable() const override;
  zx_status_t IsEnabled(bool* out_enabled) const override;

 protected:
  void StartPllChange() const override;
  void EndPllChange() const override;
  uint64_t max_freq_hz() const override { return 1'200'000'000; }

 private:
  const ddk::MmioView disable_mmio_;
  const uint32_t disable_bit_;
};

class Vs680ClockContainer {
 public:
  Vs680ClockContainer(ddk::MmioBuffer chip_ctrl_mmio, ddk::MmioBuffer cpu_pll_mmio,
                      ddk::MmioBuffer avio_mmio, zx::duration reset_time)
      : chip_ctrl_mmio_(std::move(chip_ctrl_mmio)),
        cpu_pll_mmio_(std::move(cpu_pll_mmio)),
        avio_mmio_(std::move(avio_mmio)),
        syspll0_(chip_ctrl_mmio_.View(0x200, 0x20), reset_time, chip_ctrl_mmio_.View(0x710, 4), 0),
        syspll1_(chip_ctrl_mmio_.View(0x220, 0x20), reset_time, chip_ctrl_mmio_.View(0x710, 4), 1),
        syspll2_(chip_ctrl_mmio_.View(0x240, 0x20), reset_time, chip_ctrl_mmio_.View(0x710, 4), 2),
        cpupll_(cpu_pll_mmio_.View(0, 0x20), reset_time, chip_ctrl_mmio_.View(0x710, 4), 4),
        vpll0_(avio_mmio_.View(0x04, 0x20), reset_time, avio_mmio_.View(0x130, 4), 0),
        vpll1_(avio_mmio_.View(0x70, 0x20), reset_time, avio_mmio_.View(0x130, 4), 1),
        apll0_(avio_mmio_.View(0x28, 0x20), reset_time, avio_mmio_.View(0x130, 4), 2),
        apll1_(avio_mmio_.View(0x4c, 0x20), reset_time, avio_mmio_.View(0x130, 4), 3) {}
  ~Vs680ClockContainer() {}

  void PopulateClockList(const Vs680Clock* clocks[]) const {
    clocks[vs680::kSysPll0] = &syspll0_;
    clocks[vs680::kSysPll1] = &syspll1_;
    clocks[vs680::kSysPll2] = &syspll2_;
    clocks[vs680::kCpuPll] = &cpupll_;
    clocks[vs680::kVPll0] = &vpll0_;
    clocks[vs680::kVPll1] = &vpll1_;
    clocks[vs680::kAPll0] = &apll0_;
    clocks[vs680::kAPll1] = &apll1_;
  }

 private:
  const ddk::MmioBuffer chip_ctrl_mmio_;
  const ddk::MmioBuffer cpu_pll_mmio_;
  const ddk::MmioBuffer avio_mmio_;

  const Vs680SysPll syspll0_;
  const Vs680SysPll syspll1_;
  const Vs680SysPll syspll2_;
  const Vs680CpuPll cpupll_;
  const Vs680AVPll vpll0_;
  const Vs680AVPll vpll1_;
  const Vs680AVPll apll0_;
  const Vs680AVPll apll1_;
};

}  // namespace clk

#endif  // SRC_DEVICES_CLOCK_DRIVERS_VS680_CLK_VS680_CLK_TYPES_H_
