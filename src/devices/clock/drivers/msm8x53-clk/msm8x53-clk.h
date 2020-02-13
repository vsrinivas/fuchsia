// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_CLOCK_DRIVERS_MSM8X53_CLK_MSM8X53_CLK_H_
#define SRC_DEVICES_CLOCK_DRIVERS_MSM8X53_CLK_MSM8X53_CLK_H_

#include <fuchsia/hardware/clock/c/fidl.h>
#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/thread_annotations.h>

#include <ddktl/device.h>
#include <ddktl/protocol/clockimpl.h>
#include <fbl/mutex.h>
#include <soc/msm8x53/msm8x53-clock.h>

namespace clk {

// Fwd declarations
class RcgFrequencyTable;
class MsmClkRcg;

class Msm8x53Clk;
using DeviceType = ddk::Device<Msm8x53Clk, ddk::UnbindableNew>;

class Msm8x53Clk : public DeviceType,
                   public ddk::ClockImplProtocol<Msm8x53Clk, ddk::base_protocol> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  zx_status_t Init();

  // Clock Protocol Implementation
  zx_status_t ClockImplEnable(uint32_t index);
  zx_status_t ClockImplDisable(uint32_t index);
  zx_status_t ClockImplIsEnabled(uint32_t id, bool* out_enabled);

  zx_status_t ClockImplSetRate(uint32_t id, uint64_t hz);
  zx_status_t ClockImplQuerySupportedRate(uint32_t id, uint64_t max_rate, uint64_t* out_best_rate);
  zx_status_t ClockImplGetRate(uint32_t id, uint64_t* out_current_rate);

  zx_status_t ClockImplSetInput(uint32_t id, uint32_t idx);
  zx_status_t ClockImplGetNumInputs(uint32_t id, uint32_t* out);
  zx_status_t ClockImplGetInput(uint32_t id, uint32_t* out);

  // Device Protocol Implementation.
  zx_status_t Bind();
  void DdkUnbindNew(ddk::UnbindTxn txn);
  void DdkRelease();

  // Protected for tests.
 protected:
  Msm8x53Clk(zx_device_t* parent, ddk::MmioBuffer mmio)
      : DeviceType(parent), mmio_(std::move(mmio)) {}

  enum class Toggle { Enabled, Disabled };

  // Gate Clocks
  zx_status_t GateClockEnable(uint32_t index);
  zx_status_t GateClockDisable(uint32_t index);

  // RCG Clocks
  zx_status_t RcgClockEnable(uint32_t index);
  zx_status_t RcgClockDisable(uint32_t index);
  zx_status_t RcgClockSetRate(uint32_t index, uint64_t hz) __TA_REQUIRES(rcg_rates_lock_);
  zx_status_t ToggleRcgForceEnable(uint32_t rcgr_cmd_offset, Toggle toggle);
  zx_status_t AwaitRcgEnableLocked(uint32_t rcgr_cmd_offset) __TA_REQUIRES(lock_);
  zx_status_t RcgSetRateMnd(const MsmClkRcg& clk, const RcgFrequencyTable* table);
  zx_status_t RcgSetRateHalfInteger(const MsmClkRcg& clk, const RcgFrequencyTable* table);
  zx_status_t LatchRcgConfig(const MsmClkRcg& clk);

  // Branch Clocks
  zx_status_t BranchClockEnable(uint32_t index);
  zx_status_t BranchClockDisable(uint32_t index);
  // Wait for a change to a particular branch clock to take effect.
  zx_status_t AwaitBranchClock(Toggle s, const uint32_t cbcr_reg);

  // Voter Clocks
  zx_status_t VoterClockEnable(uint32_t index);
  zx_status_t VoterClockDisable(uint32_t index);

  fbl::Mutex lock_;  // Lock guards mmio_.
  ddk::MmioBuffer mmio_;

  fbl::Mutex rcg_rates_lock_;
  uint64_t rcg_rates_[msm8x53::kRcgClkCount];
};

}  // namespace clk

#endif  // SRC_DEVICES_CLOCK_DRIVERS_MSM8X53_CLK_MSM8X53_CLK_H_
