// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_HDMI_DISPLAY_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_HDMI_DISPLAY_H_

#include <fuchsia/hardware/i2cimpl/c/banjo.h>
#include <lib/mmio/mmio-buffer.h>
#include <threads.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <cstddef>
#include <cstdint>

#include "src/graphics/display/drivers/intel-i915-tgl/ddi-physical-layer-manager.h"
#include "src/graphics/display/drivers/intel-i915-tgl/display-device.h"
#include "src/graphics/display/drivers/intel-i915-tgl/dpll.h"
#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"

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
  HdmiDisplay(Controller* controller, uint64_t id, tgl_registers::Ddi ddi,
              DdiReference ddi_reference);

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

// Returns to the list of documented DCO frequency dividers in Display PLLs.
//
// The span will remain valid for the lifetime of the process. The span's
// elements will be sorted in ascending order.
//
// The supported dividers are currently above 1 and below 100.
cpp20::span<const int8_t> DpllSupportedFrequencyDividers();

// Operating parameters for the DCO in Display PLLs.
struct DpllOscillatorConfig {
  int32_t center_frequency_khz = 0;
  int32_t frequency_khz = 0;
  int8_t frequency_divider = 0;
};

// Operating parameters for the DCO frequency dividers in Display PLLs.
//
// Unfortunately, Intel's documentation refers to the DCO dividers both as
// (P0, P1, P2) and as (P, Q, K). Fortunately, both variations use short
// names, so we can use both variations in our names below. This facilitates
// checking our code against documents that use either naming variation.
struct DpllFrequencyDividerConfig {
  int8_t p0_p_divider;
  int8_t p1_q_divider;
  int8_t p2_k_divider;
};

// Finds DPLL (Display PLL) DCO operating parameters that produce a frequency.
//
// Returns zero frequencies if no suitable frequency can be found. The DCO
// (Digitally-Controlled Oscillator) circuit has some operating constraints, and
// it's impossible to produce some frequencies given these constraints.
//
// `afe_clock_khz` is the desired frequency of the AFE (Analog Front-End) clock
// coming out of the PLL, in kHz. This is the clock frequency given to DDIs that
// use the PLL as their clock source.
//
// The AFE clock frequency must be half of the link rate supported by the DDI,
// because DDIs use both clock edges (rising and falling) to output bits. For
// protocols that use 8b/10b coding, the AFE clock frequency is 5x the symbol
// clock rate for each link lane.
DpllOscillatorConfig CreateDpllOscillatorConfig(int32_t afe_clock_khz);

// Finds a DPLL frequency divider configuration that produces `dco_divider`.
//
// `dco_divider` must be an element of `DpllSupportedFrequencyDividers()`.
DpllFrequencyDividerConfig CreateDpllFrequencyDividerConfig(int8_t dco_divider);

std::optional<HdmiDpllState> ComputeDpllConfigurationForHdmi(uint32_t symbol_clock_khz);

}  // namespace i915_tgl

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_HDMI_DISPLAY_H_
