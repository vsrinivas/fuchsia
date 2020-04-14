// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_CLOCK_DRIVERS_VS680_CLK_VS680_CLK_H_
#define SRC_DEVICES_CLOCK_DRIVERS_VS680_CLK_VS680_CLK_H_

#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/time.h>

#include <ddktl/device.h>
#include <ddktl/protocol/clockimpl.h>
#include <fbl/mutex.h>
#include <soc/vs680/vs680-clk.h>

#include "vs680-clk-types.h"

namespace {

// Wait two seconds before clearing the reset bit, as per the datasheet.
constexpr zx::duration kPllResetTime = zx::sec(2);

}  // namespace

namespace clk {

class Vs680Clk;
using DeviceType = ddk::Device<Vs680Clk>;

class Vs680Clk : public DeviceType, public ddk::ClockImplProtocol<Vs680Clk, ddk::base_protocol> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  Vs680Clk(zx_device_t* parent, ddk::MmioBuffer chip_ctrl_mmio, ddk::MmioBuffer cpu_pll_mmio,
           ddk::MmioBuffer avio_mmio, zx::duration pll_reset_time = kPllResetTime)
      : DeviceType(parent),
        clock_objects_(std::move(chip_ctrl_mmio), std::move(cpu_pll_mmio), std::move(avio_mmio),
                       pll_reset_time) {
    clock_objects_.PopulateClockList(clocks_);
  }

  void DdkRelease() { delete this; }

  zx_status_t ClockImplEnable(uint32_t id) TA_EXCL(lock_);
  zx_status_t ClockImplDisable(uint32_t id) TA_EXCL(lock_);
  zx_status_t ClockImplIsEnabled(uint32_t id, bool* out_enabled) TA_EXCL(lock_);
  zx_status_t ClockImplSetRate(uint32_t id, uint64_t hz) TA_EXCL(lock_);
  zx_status_t ClockImplQuerySupportedRate(uint32_t id, uint64_t hz, uint64_t* out_hz)
      TA_EXCL(lock_);
  zx_status_t ClockImplGetRate(uint32_t id, uint64_t* out_hz) TA_EXCL(lock_);
  zx_status_t ClockImplSetInput(uint32_t id, uint32_t idx) TA_EXCL(lock_);
  zx_status_t ClockImplGetNumInputs(uint32_t id, uint32_t* out_n) TA_EXCL(lock_);
  zx_status_t ClockImplGetInput(uint32_t id, uint32_t* out_index) TA_EXCL(lock_);

 private:
  fbl::Mutex lock_;
  const Vs680ClockContainer clock_objects_;
  // These pointers are owned by clock_objects_.
  const Vs680Clock* clocks_[vs680::kClockCount] TA_GUARDED(lock_) = {};
};

}  // namespace clk

#endif  // SRC_DEVICES_CLOCK_DRIVERS_VS680_CLK_VS680_CLK_H_
