// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_DPLL_CONFIG_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_DPLL_CONFIG_H_

#include <lib/stdcompat/span.h>

#include <cstdint>
#include <optional>

namespace i915_tgl {

// Returns to the list of documented DCO frequency dividers in Display PLLs.
//
// The span will remain valid for the lifetime of the process. The span's
// elements will be sorted in ascending order.
//
// The supported dividers are currently above 1 and below 110.
cpp20::span<const int8_t> DpllSupportedFrequencyDividersKabyLake();

// Returns to the list of documented DCO frequency dividers in Display PLLs.
//
// The span will remain valid for the lifetime of the process. The span's
// elements are not sorted in ascending order.
//
// The supported dividers are currently above 1 and below 110.
cpp20::span<const int8_t> DpllSupportedFrequencyDividersTigerLake();

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
DpllOscillatorConfig CreateDpllOscillatorConfigKabyLake(int32_t afe_clock_khz);
DpllOscillatorConfig CreateDpllOscillatorConfigForHdmiTigerLake(int32_t afe_clock_khz);
DpllOscillatorConfig CreateDpllOscillatorConfigForDisplayPortTigerLake(int32_t afe_clock_khz);

// Finds a DPLL frequency divider configuration that produces `dco_divider`.
//
// `dco_divider` must be an element of `DpllSupportedFrequencyDividers()`.
DpllFrequencyDividerConfig CreateDpllFrequencyDividerConfigKabyLake(int8_t dco_divider);
DpllFrequencyDividerConfig CreateDpllFrequencyDividerConfigTigerLake(int8_t dco_divider);

}  // namespace i915_tgl

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_DPLL_CONFIG_H_
