// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_DPLL_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_DPLL_H_

#include <lib/mmio/mmio-buffer.h>
#include <lib/zx/result.h>
#include <zircon/assert.h>

#include <string>
#include <unordered_map>
#include <variant>

#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"

namespace i915_tgl {

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
  explicit DisplayPll(tgl_registers::Dpll dpll);
  virtual ~DisplayPll() = default;

  // Implemented by platform/port-specific subclasses to enable / disable the
  // PLL.
  virtual bool Enable(const DpllState& state) = 0;
  virtual bool Disable() = 0;

  const std::string& name() const { return name_; }
  tgl_registers::Dpll dpll() const { return dpll_; }

  const DpllState& state() const { return state_; }
  void set_state(const DpllState& state) { state_ = state; }

 private:
  tgl_registers::Dpll dpll_;
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
  virtual std::optional<DpllState> LoadState(tgl_registers::Ddi ddi) = 0;

  // Finds an available display PLL for |ddi|, enables the PLL (if needed) and
  // sets the PLL state to |state|, and maps |ddi| to that PLL.
  // Returns the pointer to the PLL if it succeeds; otherwise returns |nullptr|.
  DisplayPll* Map(tgl_registers::Ddi ddi, bool is_edp, const DpllState& state);

  // Unmap the PLL associated with |ddi| and disable it if no other display is
  // using it.
  // Returns |true| if (1) |ddi| is not yet mapped to a Display PLL;
  // or (2) Unmapping and PLL disabling process succeeds.
  bool Unmap(tgl_registers::Ddi ddi);

  // Returns |true| if the PLL mapping of |ddi| needs reset, i.e.
  // - the PLL state associated with |ddi| is different from |state|, or
  // - |ddi| is not mapped to any PLL.
  bool PllNeedsReset(tgl_registers::Ddi ddi, const DpllState& state);

 private:
  virtual bool MapImpl(tgl_registers::Ddi ddi, tgl_registers::Dpll dpll) = 0;
  virtual bool UnmapImpl(tgl_registers::Ddi ddi) = 0;
  virtual DisplayPll* FindBestDpll(tgl_registers::Ddi ddi, bool is_edp, const DpllState& state) = 0;

 protected:
  std::unordered_map<tgl_registers::Dpll, std::unique_ptr<DisplayPll>> plls_;
  std::unordered_map<DisplayPll*, size_t> ref_count_;
  std::unordered_map<tgl_registers::Ddi, DisplayPll*> ddi_to_dpll_;
};

// Skylake DPLL implementation

class DpllSkylake : public DisplayPll {
 public:
  DpllSkylake(fdf::MmioBuffer* mmio_space, tgl_registers::Dpll dpll);
  ~DpllSkylake() override = default;

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

class DpllManagerSkylake : public DisplayPllManager {
 public:
  explicit DpllManagerSkylake(fdf::MmioBuffer* mmio_space);
  ~DpllManagerSkylake() override = default;

  // |DisplayPllManager|
  std::optional<DpllState> LoadState(tgl_registers::Ddi ddi) final;

 private:
  // |DisplayPllManager|
  bool MapImpl(tgl_registers::Ddi ddi, tgl_registers::Dpll dpll) final;

  // |DisplayPllManager|
  bool UnmapImpl(tgl_registers::Ddi ddi) final;

  // |DisplayPllManager|
  DisplayPll* FindBestDpll(tgl_registers::Ddi ddi, bool is_edp, const DpllState& state) final;

  fdf::MmioBuffer* mmio_space_ = nullptr;
};

// Dekel PLL (DKL PLL) for Tiger Lake
//
// Each Type-C port has a Dekel PLL tied to that port.
//
// The programming sequences for Dekel PLL is available at:
// Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev 2.0 "DKL PLL Enable Sequence", Page
// 188
class DekelPllTigerLake : public DisplayPll {
 public:
  DekelPllTigerLake(fdf::MmioBuffer* mmio_space, tgl_registers::Dpll dpll);
  ~DekelPllTigerLake() override = default;

  // |DisplayPll|
  bool Enable(const DpllState& state) final;

  // |DisplayPll|
  bool Disable() final;

  // Returns DDI enum of the DDI tied to current Dekel PLL.
  tgl_registers::Ddi ddi_id() const;

 private:
  bool EnableHdmi(const HdmiDpllState& state);
  bool EnableDp(const DpDpllState& state);

  fdf::MmioBuffer* mmio_space_ = nullptr;
  bool enabled_ = false;
};

class DpllManagerTigerLake : public DisplayPllManager {
 public:
  explicit DpllManagerTigerLake(fdf::MmioBuffer* mmio_space);
  ~DpllManagerTigerLake() override = default;

  // |DisplayPllManager|
  std::optional<DpllState> LoadState(tgl_registers::Ddi ddi) final;

 private:
  std::optional<DpllState> LoadTypeCPllState(tgl_registers::Ddi ddi);

  // |DisplayPllManager|
  bool MapImpl(tgl_registers::Ddi ddi, tgl_registers::Dpll dpll) final;

  // |DisplayPllManager|
  bool UnmapImpl(tgl_registers::Ddi ddi) final;

  // |DisplayPllManager|
  DisplayPll* FindBestDpll(tgl_registers::Ddi ddi, bool is_edp, const DpllState& state) final;

  uint32_t reference_clock_khz_ = 0u;
  fdf::MmioBuffer* mmio_space_ = nullptr;
};

}  // namespace i915_tgl

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_DPLL_H_
