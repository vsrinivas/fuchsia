// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_DISPLAY_INTEL_I915_DP_DISPLAY_H_
#define ZIRCON_SYSTEM_DEV_DISPLAY_INTEL_I915_DP_DISPLAY_H_

#include <ddk/protocol/i2cimpl.h>

#include "display-device.h"
#include "dpcd.h"

namespace i915 {

class DpAuxMessage;

class DpAux {
 public:
  DpAux(registers::Ddi ddi);

  zx_status_t I2cTransact(const i2c_impl_op_t* ops, size_t count);

  bool DpcdRead(uint32_t addr, uint8_t* buf, size_t size);
  bool DpcdWrite(uint32_t addr, const uint8_t* buf, size_t size);

  void set_mmio_space(ddk::MmioBuffer* mmio_space) {
    fbl::AutoLock lock(&lock_);
    mmio_space_ = mmio_space;
  }

 private:
  const registers::Ddi ddi_;
  // The lock protects the registers this class writes to, not the whole register io space.
  ddk::MmioBuffer* mmio_space_ __TA_GUARDED(lock_);
  mtx_t lock_;

  zx_status_t DpAuxRead(uint32_t dp_cmd, uint32_t addr, uint8_t* buf, size_t size)
      __TA_REQUIRES(lock_);
  zx_status_t DpAuxReadChunk(uint32_t dp_cmd, uint32_t addr, uint8_t* buf, uint32_t size_in,
                             size_t* size_out) __TA_REQUIRES(lock_);
  zx_status_t DpAuxWrite(uint32_t dp_cmd, uint32_t addr, const uint8_t* buf, size_t size)
      __TA_REQUIRES(lock_);
  zx_status_t SendDpAuxMsg(const DpAuxMessage& request, DpAuxMessage* reply) __TA_REQUIRES(lock_);
  zx_status_t SendDpAuxMsgWithRetry(const DpAuxMessage& request, DpAuxMessage* reply)
      __TA_REQUIRES(lock_);
};

class DpDisplay : public DisplayDevice {
 public:
  DpDisplay(Controller* controller, uint64_t id, registers::Ddi ddi);

 private:
  bool Query() final;
  bool InitDdi() final;
  bool DdiModeset(const display_mode_t& mode, registers::Pipe pipe, registers::Trans trans) final;
  bool PipeConfigPreamble(const display_mode_t& mode, registers::Pipe pipe,
                          registers::Trans trans) final;
  bool PipeConfigEpilogue(const display_mode_t& mode, registers::Pipe pipe,
                          registers::Trans trans) final;
  bool ComputeDpllState(uint32_t pixel_clock_10khz, struct dpll_state* config) final;
  uint32_t LoadClockRateForTranscoder(registers::Trans transcoder) final;

  bool CheckPixelRate(uint64_t pixel_rate) final;

  uint32_t i2c_bus_id() const final { return ddi() + registers::kDdiCount; }

  bool DpcdWrite(uint32_t addr, const uint8_t* buf, size_t size);
  bool DpcdRead(uint32_t addr, uint8_t* buf, size_t size);
  bool DpcdRequestLinkTraining(const dpcd::TrainingPatternSet& tp_set,
                               const dpcd::TrainingLaneSet lanes[]);
  bool DpcdUpdateLinkTraining(const dpcd::TrainingLaneSet lanes[]);
  template <uint32_t addr, typename T>
  bool DpcdReadPairedRegs(hwreg::RegisterBase<T, typename T::ValueType>* status);
  bool DpcdHandleAdjustRequest(dpcd::TrainingLaneSet* training, dpcd::AdjustRequestLane* adjust);
  bool DoLinkTraining();
  bool LinkTrainingSetup();
  // For locking Clock Recovery Circuit of the DisplayPort receiver
  bool LinkTrainingStage1(dpcd::TrainingPatternSet* tp_set, dpcd::TrainingLaneSet* lanes);
  // For optimizing equalization, determining symbol  boundary, and achieving inter-lane alignment
  bool LinkTrainingStage2(dpcd::TrainingPatternSet* tp_set, dpcd::TrainingLaneSet* lanes);

  bool SetBacklightOn(bool on);
  bool InitBacklightHw() override;

  bool IsBacklightOn();
  // Sets the backlight brightness with |val| as a coefficient on the maximum
  // brightness. |val| must be in [0, 1]. If the panel has a minimum fractional
  // brightness, then |val| will be clamped to [min, 1].
  bool SetBacklightBrightness(double val);

  // Gets the backlight brightness as a coefficient on the maximum brightness,
  // between the minimum brightness and 1.
  double GetBacklightBrightness();

  bool HandleHotplug(bool long_pulse) override;
  bool HasBacklight() override;
  zx_status_t SetBacklightState(bool power, double brightness) override;
  zx_status_t GetBacklightState(bool* power, double* brightness) override;

  uint8_t dpcd_capability(uint16_t addr) { return dpcd_capabilities_[addr - dpcd::DPCD_CAP_START]; }
  uint8_t dpcd_edp_capability(uint16_t addr) {
    return dpcd_edp_capabilities_[addr - dpcd::DPCD_EDP_CAP_START];
  }

  uint8_t dp_lane_count_;
  uint32_t dp_link_rate_mhz_;
  uint8_t dp_link_rate_idx_plus1_;
  bool dp_enhanced_framing_enabled_;

  uint8_t dpcd_capabilities_[16];
  uint8_t dpcd_edp_capabilities_[5];
  bool backlight_aux_brightness_;
  bool backlight_aux_power_;

  // The backlight brightness coefficient, in the range [min brightness, 1].
  double backlight_brightness_ = 1.0f;
};

}  // namespace i915

#endif  // ZIRCON_SYSTEM_DEV_DISPLAY_INTEL_I915_DP_DISPLAY_H_
