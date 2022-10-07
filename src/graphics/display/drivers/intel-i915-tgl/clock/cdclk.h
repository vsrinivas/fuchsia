// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_CLOCK_CDCLK_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_CLOCK_CDCLK_H_

#include <lib/mmio/mmio-buffer.h>
#include <lib/mmio/mmio.h>
#include <zircon/assert.h>

namespace i915_tgl {

class CoreDisplayClock {
 public:
  CoreDisplayClock() = default;
  virtual ~CoreDisplayClock() = default;

  // Returns true if the target CDCLK frequency |freq_khz| is supported by the
  // current platform and current hardware config.
  virtual bool CheckFrequency(uint32_t freq_khz) = 0;

  // Sets CDCLK frequency to |freq_khz|, if the frequency is supported by the
  // current hardware config.
  // Returns false if there's any error when setting the frequency; otherwise
  // returns true.
  virtual bool SetFrequency(uint32_t freq_khz) = 0;

  uint32_t current_freq_khz() const { return current_freq_khz_; }

 protected:
  virtual bool LoadState() = 0;

  void set_current_freq_khz(uint32_t freq_khz) { current_freq_khz_ = freq_khz; }

 private:
  uint32_t current_freq_khz_ = 0u;
};

// Skylake CD Clock

class CoreDisplayClockSkylake final : public CoreDisplayClock {
 public:
  explicit CoreDisplayClockSkylake(fdf::MmioBuffer* mmio_space);
  ~CoreDisplayClockSkylake() override = default;

  bool CheckFrequency(uint32_t freq_khz) override;
  bool SetFrequency(uint32_t freq_khz) override;

 private:
  bool LoadState() override;

  bool PreChangeFreq();
  bool ChangeFreq(uint32_t freq_khz);
  bool PostChangeFreq(uint32_t freq_khz);

  static int VoltageLevelForFrequency(uint32_t frequency_khz);

  fdf::MmioBuffer* mmio_space_ = nullptr;
};

// Tiger Lake CD Clock

class CoreDisplayClockTigerLake final : public CoreDisplayClock {
 public:
  explicit CoreDisplayClockTigerLake(fdf::MmioBuffer* mmio_space);
  ~CoreDisplayClockTigerLake() override = default;

  // Clients could set |freq_khz| to 0 to disable the CDCLK PLL, or
  // set it to a non-zero value to enable the PLL; |freq_khz| should
  // match the device's reference clock frequency (see
  // intel-gfx-prm-osrc-tgl-vol12-displayengine_0.pdf P178).
  bool CheckFrequency(uint32_t freq_khz) override;
  bool SetFrequency(uint32_t freq_khz) override;

 private:
  struct State {
    uint32_t cd2x_divider = 1;
    uint32_t pll_ratio = 1;
  };
  std::optional<State> FreqToState(uint32_t freq_khz) const;

  bool LoadState() override;

  bool PreChangeFreq();
  bool ChangeFreq(uint32_t freq_khz);
  bool PostChangeFreq(uint32_t freq_khz);

  static int VoltageLevelForFrequency(uint32_t frequency_khz);

  bool Disable();
  bool Enable(uint32_t freq_khz, State state);

  fdf::MmioBuffer* mmio_space_ = nullptr;

  uint32_t ref_clock_khz_ = 0;
  State state_ = {};
  bool enabled_ = false;
};

}  // namespace i915_tgl

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_CLOCK_CDCLK_H_
