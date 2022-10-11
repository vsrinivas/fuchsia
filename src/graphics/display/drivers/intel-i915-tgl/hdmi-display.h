// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_HDMI_DISPLAY_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_HDMI_DISPLAY_H_

#include <fuchsia/hardware/i2cimpl/c/banjo.h>

#include "src/graphics/display/drivers/intel-i915-tgl/display-device.h"

namespace i915_tgl {

class GMBusI2c {
 public:
  GMBusI2c(tgl_registers::Ddi ddi, fdf::MmioBuffer* mmio_space);
  zx_status_t I2cTransact(const i2c_impl_op_t* ops, size_t count);

 private:
  const tgl_registers::Ddi ddi_;
  // The lock protects the registers this class writes to, not the whole register io space.
  fdf::MmioBuffer* mmio_space_ __TA_GUARDED(lock_);
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
  HdmiDisplay(Controller* controller, uint64_t id, tgl_registers::Ddi ddi);

  HdmiDisplay(const HdmiDisplay&) = delete;
  HdmiDisplay(HdmiDisplay&&) = delete;
  HdmiDisplay& operator=(const HdmiDisplay&) = delete;
  HdmiDisplay& operator=(HdmiDisplay&&) = delete;

  ~HdmiDisplay() override;

 private:
  bool InitDdi() final;
  bool Query() final;
  bool DdiModeset(const display_mode_t& mode) final;
  bool PipeConfigPreamble(const display_mode_t& mode, tgl_registers::Pipe pipe,
                          tgl_registers::Trans transcoder) final;
  bool PipeConfigEpilogue(const display_mode_t& mode, tgl_registers::Pipe pipe,
                          tgl_registers::Trans transcoder) final;
  bool ComputeDpllState(uint32_t pixel_clock_10khz, DpllState* config) final;
  // Hdmi doesn't need the clock rate when chaning the transcoder
  uint32_t LoadClockRateForTranscoder(tgl_registers::Trans transcoder) final { return 0; }

  bool CheckPixelRate(uint64_t pixel_rate) final;

  uint32_t i2c_bus_id() const final { return 2 * ddi() + 1; }
};

bool ComputeDpllConfigurationForHdmi(uint32_t symbol_clock_khz, uint16_t* dco_int,
                                     uint16_t* dco_frac, uint8_t* q, uint8_t* q_mode, uint8_t* k,
                                     uint8_t* p, uint8_t* cf);

}  // namespace i915_tgl

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_HDMI_DISPLAY_H_
