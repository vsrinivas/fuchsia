// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_DPLL_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_DPLL_H_

#include <lib/mmio/mmio-buffer.h>
#include <lib/zx/result.h>
#include <zircon/assert.h>

#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "src/graphics/display/drivers/intel-i915/registers-ddi.h"
#include "src/graphics/display/drivers/intel-i915/registers-dpll.h"
#include "src/graphics/display/drivers/intel-i915/registers.h"

namespace i915 {

struct DpDpllState {
  // Bit rate (Mbps / MHz) of one DP lane.
  uint32_t dp_bit_rate_mhz;
};

struct HdmiDpllState {
  // Integer part of DCO frequency.
  uint16_t dco_int;
  // Fractional part of DCO frequency:
  // (DCO Frequency/24 - INT(DCO Frequency/24)) * 2^15
  uint16_t dco_frac;
  // |p|, |q| and |k| are dividers to calculate the PLL output frequency.
  // PLL output frequency = DCO Frequency / (p * q * k)
  uint8_t q;
  // |q_mode| enables |q| divider.
  uint8_t q_mode;
  uint8_t k;
  uint8_t p;
  // Central frequency.
  uint8_t cf;
};

using DpllState = std::variant<DpDpllState, HdmiDpllState>;

class DisplayPllManager;
class DisplayPll;

class DisplayPll {
 public:
  explicit DisplayPll(registers::Dpll dpll);
  virtual ~DisplayPll() = default;

  // Implemented by platform/port-specific subclasses to enable / disable the
  // PLL.
  virtual bool Enable(const DpllState& state) = 0;
  virtual bool Disable() = 0;

  const std::string& name() const { return name_; }
  registers::Dpll dpll() const { return dpll_; }

  const DpllState& state() const { return state_; }
  void set_state(const DpllState& state) { state_ = state; }

 private:
  registers::Dpll dpll_;
  std::string name_;

  DpllState state_ = {};
};

class DisplayPllManager {
 public:
  DisplayPllManager() = default;
  virtual ~DisplayPllManager() = default;

  // Loads PLL mapping and PLL state for |ddi| from hardware registers directly.
  // Returns loaded state on successful loading; returns |nullopt| on
  // failure.
  virtual std::optional<DpllState> LoadState(registers::Ddi ddi) = 0;

  // Finds an available display PLL for |ddi|, enables the PLL (if needed) and
  // sets the PLL state to |state|, and maps |ddi| to that PLL.
  // Returns the pointer to the PLL if it succeeds; otherwise returns |nullptr|.
  DisplayPll* Map(registers::Ddi ddi, bool is_edp, const DpllState& state);

  // Unmap the PLL associated with |ddi| and disable it if no other display is
  // using it.
  // Returns |true| if (1) |ddi| is not yet mapped to a Display PLL;
  // or (2) Unmapping and PLL disabling process succeeds.
  bool Unmap(registers::Ddi ddi);

  // Returns |true| if the PLL mapping of |ddi| needs reset, i.e.
  // - the PLL state associated with |ddi| is different from |state|, or
  // - |ddi| is not mapped to any PLL.
  bool PllNeedsReset(registers::Ddi ddi, const DpllState& state);

 private:
  virtual bool MapImpl(registers::Ddi ddi, registers::Dpll dpll) = 0;
  virtual bool UnmapImpl(registers::Ddi ddi) = 0;
  virtual DisplayPll* FindBestDpll(registers::Ddi ddi, bool is_edp, const DpllState& state) = 0;

 protected:
  std::vector<std::unique_ptr<DisplayPll>> plls_;
  std::unordered_map<DisplayPll*, size_t> ref_count_;
  std::unordered_map<registers::Ddi, DisplayPll*> ddi_to_dpll_;
};

// Skylake DPLL implementation

class SklDpll : public DisplayPll {
 public:
  SklDpll(fdf::MmioBuffer* mmio_space, registers::Dpll dpll);
  ~SklDpll() override = default;

  // |DisplayPll|
  bool Enable(const DpllState& state) final;

  // |DisplayPll|
  bool Disable() final;

 private:
  bool EnableHdmi(const HdmiDpllState& state);
  bool EnableDp(const DpDpllState& state);

  fdf::MmioBuffer* mmio_space_ = nullptr;
  bool enabled_ = false;
};

class SklDpllManager : public DisplayPllManager {
 public:
  explicit SklDpllManager(fdf::MmioBuffer* mmio_space);
  ~SklDpllManager() override = default;

  // |DisplayPllManager|
  std::optional<DpllState> LoadState(registers::Ddi ddi) final;

 private:
  // |DisplayPllManager|
  bool MapImpl(registers::Ddi ddi, registers::Dpll dpll) final;

  // |DisplayPllManager|
  bool UnmapImpl(registers::Ddi ddi) final;

  // |DisplayPllManager|
  DisplayPll* FindBestDpll(registers::Ddi ddi, bool is_edp, const DpllState& state) final;

  fdf::MmioBuffer* mmio_space_ = nullptr;
};

}  // namespace i915

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_DPLL_H_
