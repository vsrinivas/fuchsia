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

// High-level configuration of a PLL that serves as a DDI clock source.
//
// The information included here is used to decide whether a PLL (Phase-Locked
// Loop circuit) that is already configured in a certain way can serve as the
// clock source for a DDI that is being configured.
//
// This structure omits some low-level details needed to configure a PLL for DDI
// usage. The omitted details are fully determined by (and can be derived from)
// the information here.
struct DdiPllConfig {
  // The default-constructed instance is empty.
  DdiPllConfig() = default;

  // The DDI clock rate.
  //
  // This is half the bitrate on each link lane, because DDIs use both clock
  // edges (rising and falling) to push bits onto the links.
  int32_t ddi_clock_khz = 0;

  // True if the PLL output uses SSC (Spread Spectrum Clocking).
  bool spread_spectrum_clocking = false;

  // True if this DPLL can be used for DisplayPort links.
  bool admits_display_port = false;

  // True if this DPLL can be used for HDMI links.
  bool admits_hdmi = false;

  // True for configurations that may lead to correct hardware operation.
  //
  // This method is intended to be used as a precondition check. Invalid
  // configurations are definitely not suitable for use with hardware.
  bool IsValid() const;

  // True for invalid configurations that mean "no value".
  //
  // The empty value is intended for reporting "not found" errors, such as
  // not finding a valid configuration that meets some constraints.
  bool IsEmpty() const { return ddi_clock_khz == 0; }
};

bool operator==(const DdiPllConfig& lhs, const DdiPllConfig& rhs) noexcept;
bool operator!=(const DdiPllConfig& lhs, const DdiPllConfig& rhs) noexcept;

// Manages a PLL (Phase-Locked Loop circuit) that serves as a DDI clock source.
//
// This is an abstract base class. Subclasses implement the configuration
// protocols, which are specific to each type of PLL.
class DisplayPll {
 public:
  virtual ~DisplayPll() = default;

  DisplayPll(const DisplayPll&) = delete;
  DisplayPll(DisplayPll&&) = delete;
  DisplayPll& operator=(const DisplayPll&) = delete;
  DisplayPll& operator=(DisplayPll&&) = delete;

  // Configures this PLL and waits for it to lock.
  //
  // Returns true if the PLL is locked to the desired configuration. Returns
  // false if something went wrong.
  //
  // `pll_config` must be valid.
  //
  // This method is not idempotent. The PLL must not already be enabled.
  bool Enable(const DdiPllConfig& pll_config);

  // Disables this PLL. Also powers off the PLL, if possible.
  //
  // The PLL must not be used as a clock source by any of the powered-up DDIs.
  //
  // This method is not idempotent. The PLL must be locked to a configuration
  // by a successful Enable() call.
  bool Disable();

  const std::string& name() const { return name_; }
  tgl_registers::Dpll dpll() const { return dpll_; }

  // The configuration that the PLL is locked to.
  //
  // Returns an empty configuration if the PLL is disabled.
  const DdiPllConfig& config() const { return config_; }

 protected:
  explicit DisplayPll(tgl_registers::Dpll dpll);

  // Same API as `Enable()`.
  //
  // Implementations can assume that logging and state updating are taken care
  // of, and focus on the register-level configuration.
  virtual bool DoEnable(const DdiPllConfig& pll_config) = 0;

  // Same API as `Disable()`.
  //
  // Implementations can assume that logging and state updating are taken care
  // of, and focus on the register-level configuration.
  virtual bool DoDisable() = 0;

  // See `config()` for details.
  void set_config(const DdiPllConfig& config) { config_ = config; }

 private:
  tgl_registers::Dpll dpll_;
  std::string name_;

  DdiPllConfig config_ = {};
};

// Tracks all the PLLs used as DDI clock sources in a display engine.
class DisplayPllManager {
 public:
  virtual ~DisplayPllManager() = default;

  DisplayPllManager(const DisplayPllManager&) = delete;
  DisplayPllManager(DisplayPllManager&&) = delete;
  DisplayPllManager& operator=(const DisplayPllManager&) = delete;
  DisplayPllManager& operator=(DisplayPllManager&&) = delete;

  // Returns the DDI clock configuration for `ddi`.
  //
  // Returns an empty `DdiPllConfig` if the DDI does not have a PLL configured
  // as its clock source, if the PLL is not enabled, or if the PLL configuration
  // is invalid. Otherwise, returns a valid DdiPllConfig.
  //
  // TODO(fxbug.com/112752): This API needs to be revised.
  virtual DdiPllConfig LoadState(tgl_registers::Ddi ddi) = 0;

  // Configures a DDI's clock source to match the desired configuration.
  //
  // On success, returns the PLL configured as the DDI's clock source. On
  // failure, returns null.
  //
  // `ddi` must be usable on this display engine (not fused off), disabled and
  // powered down. Use `LoadState()` to have the manager reflect an association
  // between a powered-up DDI and its clock source.
  //
  // `pll_config` must be valid.
  //
  // This process entails finding a PLL that can be used as this DDI's clock
  // source, configuring the PLL, waiting for the PLL to lock, and associating
  // the PLL with the DDI. If any of these steps fails, the entire operation is
  // considered to have failed.
  DisplayPll* SetDdiPllConfig(tgl_registers::Ddi ddi, bool is_edp,
                              const DdiPllConfig& desired_config);

  // Resets a DDI's clock source configuration.
  //
  // Returns true if the DDI's clock source is reset. This method is idempotent,
  // so it will return true when called with a DDI without a configured clock
  // source.
  //
  // `ddi` must be usable on this display engine (not fused off), disabled and
  // powered down.
  //
  // This method is idempotent. It (quickly) succeeds if the DDI does not have a
  // clock source.
  //
  // If the PLL that served as the DDI's clock source becomes unused after this
  // operation, the PLL is disabled and powered down, if possible.
  bool ResetDdiPll(tgl_registers::Ddi ddi);

  // True if the PLL configured as a DDI's clock source matches a configuration.
  //
  // Returns false if the DDI does not have any clock source configured.
  //
  // `ddi` must be usable on this display engine (not fused off).
  bool DdiPllMatchesConfig(tgl_registers::Ddi ddi, const DdiPllConfig& desired_config);

 protected:
  DisplayPllManager() = default;

  // Configures a PLL to serve as a DDI's clock source.
  //
  // `pll` must be locked to the desired configuration. `ddi` must be usable on
  // this display engine (not fused off), disabled and powered down. `pll` must
  // be usable as a source clock for `ddi`.
  //
  // This method is idempotent. It succeeds if `ddi` already has `pll`
  // configured as its clock source.
  //
  // Implementations perform the register-level configuration, while assuming
  // that logging and state updating are taken care of.
  virtual bool SetDdiClockSource(tgl_registers::Ddi ddi, tgl_registers::Dpll pll) = 0;

  // Resets the DDI's clock source so it doesn't use any PLL.
  //
  // `ddi` must be usable on this display engine (not fused off), disabled and
  // powered down.
  //
  // This method is idempotent. It succeeds if `ddi` does not have any clock
  // source.
  //
  // Implementations perform the register-level configuration, while assuming
  // that logging and state updating are taken care of.
  virtual bool ResetDdiClockSource(tgl_registers::Ddi ddi) = 0;

  // Returns the most suitable PLL to serve as a DDI's clock source.
  //
  // Returns null if the search fails. On success, returns a `DisplayPll` for a
  // PLL that is either unused, or is already locked to the desired
  // configuration.
  //
  // `ddi` must be usable on this display engine (not fused off), disabled and
  // powered down. `desired_config` must be valid.
  //
  // Implementations perform the register-level configuration, while assuming
  // that logging and state updating are taken care of.
  virtual DisplayPll* FindPllFor(tgl_registers::Ddi ddi, bool is_edp,
                                 const DdiPllConfig& desired_config) = 0;

  std::unordered_map<tgl_registers::Dpll, std::unique_ptr<DisplayPll>> plls_;
  std::unordered_map<DisplayPll*, size_t> ref_count_;
  std::unordered_map<tgl_registers::Ddi, DisplayPll*> ddi_to_dpll_;
};

// DPLL (Display PLL) for Kaby Lake and Skylake display engines.
//
// DPLLs are shareable across multiple DDIs. DPLL 0 is special-cased on Kaby
// Lake and Skylake, because its VCO (Voltage-Controlled Oscillator) output is
// also used to drive the CDCLK (core display clock).
class DpllSkylake : public DisplayPll {
 public:
  DpllSkylake(fdf::MmioBuffer* mmio_space, tgl_registers::Dpll dpll);
  ~DpllSkylake() override = default;

 protected:
  // DisplayPll overrides:
  bool DoEnable(const DdiPllConfig& pll_config) final;
  bool DoDisable() final;

 private:
  bool ConfigureForHdmi(const DdiPllConfig& pll_config);
  bool ConfigureForDisplayPort(const DdiPllConfig& pll_config);

  fdf::MmioBuffer* mmio_space_ = nullptr;
};

class DpllManagerSkylake : public DisplayPllManager {
 public:
  explicit DpllManagerSkylake(fdf::MmioBuffer* mmio_space);
  ~DpllManagerSkylake() override = default;

  // DisplayPllManager overrides:
  DdiPllConfig LoadState(tgl_registers::Ddi ddi) final;

 private:
  // DisplayPllManager overrides:
  bool SetDdiClockSource(tgl_registers::Ddi ddi, tgl_registers::Dpll pll) final;
  bool ResetDdiClockSource(tgl_registers::Ddi ddi) final;
  DisplayPll* FindPllFor(tgl_registers::Ddi ddi, bool is_edp,
                         const DdiPllConfig& desired_config) final;

  fdf::MmioBuffer* mmio_space_ = nullptr;
};

// Display PLL (DPLL) for Tiger Lake display engines.
//
// DPLLs are shareable across Combo PHYs. Multiple PHYs can use the same DPLL,
// as long as they require the same frequency and SSC (Spread-Spectrum Clocking)
// characteristics.
class DisplayPllTigerLake : public DisplayPll {
 public:
  DisplayPllTigerLake(fdf::MmioBuffer* mmio_space, tgl_registers::Dpll dpll);
  ~DisplayPllTigerLake() override = default;

 protected:
  // DisplayPll overrides:
  bool DoEnable(const DdiPllConfig& pll_config) final;
  bool DoDisable() final;

 private:
  fdf::MmioBuffer* mmio_space_ = nullptr;
};

// DKL (Dekel) PLLs for Tiger Lake display engines.
//
// Each TC (Type-C) DDI has a dedicated PLL tied to it.
class DekelPllTigerLake : public DisplayPll {
 public:
  DekelPllTigerLake(fdf::MmioBuffer* mmio_space, tgl_registers::Dpll dpll);
  ~DekelPllTigerLake() override = default;

  // Returns DDI enum of the DDI tied to current Dekel PLL.
  tgl_registers::Ddi ddi_id() const;

 protected:
  // DisplayPll overrides:
  bool DoEnable(const DdiPllConfig& pll_config) final;
  bool DoDisable() final;

 private:
  bool EnableHdmi(const DdiPllConfig& pll_config);
  bool EnableDp(const DdiPllConfig& pll_config);

  fdf::MmioBuffer* mmio_space_ = nullptr;
};

class DpllManagerTigerLake : public DisplayPllManager {
 public:
  explicit DpllManagerTigerLake(fdf::MmioBuffer* mmio_space);
  ~DpllManagerTigerLake() override = default;

  // DisplayPllManager overrides:
  DdiPllConfig LoadState(tgl_registers::Ddi ddi) final;

 private:
  DdiPllConfig LoadStateForComboDdi(tgl_registers::Ddi ddi);
  DdiPllConfig LoadStateForTypeCDdi(tgl_registers::Ddi ddi);

  // DisplayPllManager overrides:
  bool SetDdiClockSource(tgl_registers::Ddi ddi, tgl_registers::Dpll pll) final;
  bool ResetDdiClockSource(tgl_registers::Ddi ddi) final;
  DisplayPll* FindPllFor(tgl_registers::Ddi ddi, bool is_edp,
                         const DdiPllConfig& desired_config) final;

  uint32_t reference_clock_khz_ = 0u;
  fdf::MmioBuffer* mmio_space_ = nullptr;
};

}  // namespace i915_tgl

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_DPLL_H_
