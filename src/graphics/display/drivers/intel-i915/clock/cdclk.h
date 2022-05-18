// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_CLOCK_CDCLK_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_CLOCK_CDCLK_H_

#include <lib/mmio/mmio-buffer.h>
#include <lib/mmio/mmio.h>
#include <zircon/assert.h>

namespace i915 {

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

class SklCoreDisplayClock final : public CoreDisplayClock {
 public:
  explicit SklCoreDisplayClock(fdf::MmioBuffer* mmio_space);
  ~SklCoreDisplayClock() override = default;

  bool CheckFrequency(uint32_t freq_khz) override;
  bool SetFrequency(uint32_t freq_khz) override;

 private:
  bool LoadState() override;

  bool PreChangeFreq();
  bool ChangeFreq(uint32_t freq_khz);
  bool PostChangeFreq(uint32_t freq_khz);

  fdf::MmioBuffer* mmio_space_ = nullptr;
};

}  // namespace i915

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_CLOCK_CDCLK_H_
