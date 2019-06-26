// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_DISPLAY_INTEL_I915_HDMI_DISPLAY_H_
#define ZIRCON_SYSTEM_DEV_DISPLAY_INTEL_I915_HDMI_DISPLAY_H_

#include <ddk/protocol/i2cimpl.h>

#include "display-device.h"

namespace i915 {

class GMBusI2c {
 public:
  GMBusI2c(registers::Ddi ddi);
  zx_status_t I2cTransact(const i2c_impl_op_t* ops, size_t count);

  void set_mmio_space(ddk::MmioBuffer* mmio_space) {
    fbl::AutoLock lock(&lock_);
    mmio_space_ = mmio_space;
  }

 private:
  const registers::Ddi ddi_;
  // The lock protects the registers this class writes to, not the whole register io space.
  ddk::MmioBuffer* mmio_space_ __TA_GUARDED(lock_);
  mtx_t lock_;

  bool I2cFinish() __TA_REQUIRES(lock_);
  bool I2cWaitForHwReady() __TA_REQUIRES(lock_);
  bool I2cClearNack() __TA_REQUIRES(lock_);
  bool SetDdcSegment(uint8_t block_num) __TA_REQUIRES(lock_);
  bool GMBusRead(uint8_t addr, uint8_t* buf, uint8_t size) __TA_REQUIRES(lock_);
  bool GMBusWrite(uint8_t addr, const uint8_t* buf, uint8_t size) __TA_REQUIRES(lock_);
};

class HdmiDisplay : public DisplayDevice {
 public:
  HdmiDisplay(Controller* controller, uint64_t id, registers::Ddi ddi);

 private:
  bool InitDdi() final;
  bool Query() final;
  bool DdiModeset(const display_mode_t& mode, registers::Pipe pipe, registers::Trans trans) final;
  bool PipeConfigPreamble(const display_mode_t& mode, registers::Pipe pipe,
                          registers::Trans trans) final;
  bool PipeConfigEpilogue(const display_mode_t& mode, registers::Pipe pipe,
                          registers::Trans trans) final;
  bool ComputeDpllState(uint32_t pixel_clock_10khz, struct dpll_state* config) final;
  // Hdmi doesn't need the clock rate when chaning the transcoder
  uint32_t LoadClockRateForTranscoder(registers::Trans transcoder) final { return 0; }

  bool CheckPixelRate(uint64_t pixel_rate) final;

  uint32_t i2c_bus_id() const final { return ddi(); }
};

}  // namespace i915

#endif  // ZIRCON_SYSTEM_DEV_DISPLAY_INTEL_I915_HDMI_DISPLAY_H_
