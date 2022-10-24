// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/dpll-config.h"

#include <lib/stdcompat/span.h>
#include <zircon/assert.h>

#include <cstdint>
#include <limits>

namespace i915_tgl {

cpp20::span<const int8_t> DpllSupportedFrequencyDividersKabyLake() {
  // This list merges the odd and even dividers in  the "Pseudocode to Find HDMI
  // and DVI DPLL Programming" section in the display engine PRMs.
  //
  // The register-level reference sugggests that there are valid dividers that
  // are not listed here. For example, any multiple of 4 below 1024 can be
  // achieved using K (P0) = 2, Q (P1) = 1-255, P (P2) = 2.
  //
  // Kaby Lake: IHD-OS-KBL-Vol 12-1.17 pages 135-136
  // Skylake: IHD-OS-SKL-Vol 12-05.16 pages 132-133
  static constexpr int8_t kDividers[] = {3,  4,  5,  6,  7,  8,  9,  10, 12, 14, 15, 16, 18, 20,
                                         21, 24, 28, 30, 32, 36, 40, 42, 44, 48, 52, 54, 56, 60,
                                         64, 66, 68, 70, 72, 76, 78, 80, 84, 88, 90, 92, 96, 98};
  return kDividers;
}

cpp20::span<const int8_t> DpllSupportedFrequencyDividersTigerLake() {
  // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev2.0 pages 181-182

  // TODO(costan): These aren't ordered anymore.
  static constexpr int8_t kDividers[] = {
      2,  4,  6,  8,  10, 12, 14, 16, 18, 20, 24, 28, 30, 32, 36, 40,  42,  44, 48, 50, 52, 54, 56,
      60, 64, 66, 68, 70, 72, 76, 78, 80, 84, 88, 90, 92, 96, 98, 100, 102, 3,  5,  7,  9,  15, 21};

  return kDividers;
}

DpllOscillatorConfig CreateDpllOscillatorConfigKabyLake(int32_t afe_clock_khz) {
  ZX_ASSERT(afe_clock_khz > 0);

  // The implementation conceptually follows the big `For` loop in the
  // "Pseudocode to Find HDMI and DVI DPLL Programming" section in the display
  // engine PRMs.
  //
  // Kaby Lake: IHD-OS-KBL-Vol 12-1.17 pages 135-136
  // Skylake: IHD-OS-SKL-Vol 12-05.16 pages 132-133

  static constexpr int32_t kCenterFrequenciesKhz[] = {8'400'000, 9'000'000, 9'600'000};

  DpllOscillatorConfig result;
  int32_t min_deviation = std::numeric_limits<int32_t>::max();

  const cpp20::span<const int8_t> supported_dividers = DpllSupportedFrequencyDividersKabyLake();

  // The PRM asks that we prefer even frequency dividers so strongly that we'll
  // chose any acceptable DPLL configuration with an even divider over any
  // configuration with an old divider.
  static constexpr bool kWantEvenDivider[] = {true, false};
  for (const bool& want_even_divider : kWantEvenDivider) {
    for (const int32_t& center_frequency_khz : kCenterFrequenciesKhz) {
      // The DCO frequency must be within [-6%, +1%] of the center DCO
      // frequency. We compute the ends of this range below.
      //
      // The DCO frequencies are all in the Mhz range, so the divisions below
      // are exact. `max_frequency_khz` and `min_frequency_khz` are at most
      // 9,696,000.
      const int32_t max_frequency_khz = center_frequency_khz + (center_frequency_khz / 100);
      const int32_t min_frequency_khz = center_frequency_khz - 6 * (center_frequency_khz / 100);

      // The PLL output (AFE clock) frequency is the DCO (Digitally-Controlled
      // Oscillator) frequency divided by the frequency divider. More compactly,
      //     AFE clock frequency = DCO frequency / divider
      //
      // Rearranging terms gives us the following equations we'll use below.
      //     DCO frequency = AFE clock frequency * divider
      //     divider = DCO frequency / AFE clock frequency
      //
      // The target AFE clock frequency is fixed (given to this function), and
      // there is an acceptable range of the DCO frequencies. This leads to an
      // acceptable range of dividers, computed below.
      //
      // All supported dividers are integers. In order to stay within the range,
      // we must round down the maximum divider and round up the minimum
      // divider.
      const int32_t max_divider = max_frequency_khz / afe_clock_khz;
      const int32_t min_divider = (min_frequency_khz + afe_clock_khz - 1) / afe_clock_khz;
      if (max_divider < supported_dividers.front() || min_divider > supported_dividers.back()) {
        continue;
      }

      // Iterate over all supported frequency divider values, and save the value
      // that gives the lowest deviation from the DCO center frequency. The
      // number of supported dividers is small enough that binary search
      // wouldn't yield a meaningful improvement.
      for (const int8_t& candidate_divider : supported_dividers) {
        if (candidate_divider > max_divider) {
          break;
        }
        if (candidate_divider < min_divider) {
          continue;
        }
        const bool is_divider_even = (candidate_divider % 2) == 0;
        if (is_divider_even != want_even_divider) {
          continue;
        }

        // The multiplication will not overflow (causing UB) because the result
        // is guaranteed to fall in the range of `min_frequency_khz` and
        // `max_frequency_khz`. This is because of the range checks on
        // `candidate_divider` above.
        const int32_t frequency_khz = static_cast<int32_t>(candidate_divider * afe_clock_khz);
        ZX_DEBUG_ASSERT(frequency_khz >= min_frequency_khz);
        ZX_DEBUG_ASSERT(frequency_khz <= max_frequency_khz);

        // `dco_frequency_khz` is within [-6%, +1%] of `dco_frequency_khz`, so
        // the maximum `absolute_difference` is 6% of the highest DCO center
        // frequency, which is 5,760,000.
        const int32_t absolute_deviation = std::abs(frequency_khz - center_frequency_khz);

        // We follow the pseudocode in spirit, by computing the ratio between
        // the frequency difference and the center frequency. We avoid using
        // floating-point computation by scaling the difference by 1,000,000
        // before the division.
        //
        // The range for `absolute_deviation` dictates that the multiplication
        // below uses 64-bit integers. At the same time, the division result
        // will be at most 6% of 1,000,000, which fits comfortably in a 32-bit
        // integer.
        const int32_t relative_deviation =
            static_cast<int32_t>((int64_t{1'000'000} * absolute_deviation) / center_frequency_khz);
        if (relative_deviation < min_deviation) {
          min_deviation = relative_deviation;
          result = DpllOscillatorConfig{
              .center_frequency_khz = center_frequency_khz,
              .frequency_khz = frequency_khz,
              .frequency_divider = candidate_divider,
          };
        }
      }
    }

    if (result.frequency_divider != 0) {
      break;
    }
  }

  return result;
}

DpllOscillatorConfig CreateDpllOscillatorConfigForHdmiTigerLake(int32_t afe_clock_khz) {
  ZX_ASSERT(afe_clock_khz > 0);

  // The implementation conceptually follows the big `foreach` loop in the
  // the "Pseudo-code for HDMI Mode DPLL Programming" section in the display
  // engine PRMs.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev2.0 pages 181-182

  static constexpr int32_t kMinFrequencyKhz = 7'998'000;
  static constexpr int32_t kMaxFrequencyKhz = 10'000'000;
  static constexpr int32_t kCenterFrequencyKhz = 8'999'000;

  DpllOscillatorConfig result;
  int32_t min_deviation = std::numeric_limits<int32_t>::max();

  const cpp20::span<const int8_t> supported_dividers = DpllSupportedFrequencyDividersTigerLake();

  // The PLL output (AFE clock) frequency is the DCO (Digitally-Controlled
  // Oscillator) frequency divided by the frequency divider. More compactly,
  //     AFE clock frequency = DCO frequency / divider
  //
  // Rearranging terms gives us the following equations we'll use below.
  //     DCO frequency = AFE clock frequency * divider
  //     divider = DCO frequency / AFE clock frequency
  //
  // The target AFE clock frequency is fixed (given to this function), and
  // there is an acceptable range of the DCO frequencies. This leads to an
  // acceptable range of dividers, computed below.
  //
  // All supported dividers are integers. In order to stay within the range,
  // we must round down the maximum divider and round up the minimum
  // divider.
  const int32_t max_divider = kMaxFrequencyKhz / afe_clock_khz;
  const int32_t min_divider = (kMinFrequencyKhz + afe_clock_khz - 1) / afe_clock_khz;

  // Iterate over all supported frequency divider values, and save the value
  // that gives the lowest deviation from the DCO center frequency. The
  // number of supported dividers is small enough that binary search
  // wouldn't yield a meaningful improvement.
  for (const int8_t& candidate_divider : supported_dividers) {
    if (candidate_divider < min_divider || candidate_divider > max_divider) {
      continue;
    }

    // The multiplication will not overflow (causing UB) because the result
    // is guaranteed to fall in the range of `min_frequency_khz` and
    // `max_frequency_khz`. This is because of the range checks on
    // `candidate_divider` above.
    const int32_t frequency_khz = static_cast<int32_t>(candidate_divider * afe_clock_khz);
    ZX_DEBUG_ASSERT(frequency_khz >= kMinFrequencyKhz);
    ZX_DEBUG_ASSERT(frequency_khz <= kMaxFrequencyKhz);

    // `dco_frequency_khz` is within [-12%, +12%] of `dco_frequency_khz`, so
    // the maximum `absolute_difference` is 12% of the highest DCO center
    // frequency, which is 1,152,000.
    const int32_t absolute_deviation = std::abs(frequency_khz - kCenterFrequencyKhz);

    if (absolute_deviation < min_deviation) {
      min_deviation = absolute_deviation;
      result = DpllOscillatorConfig{
          .center_frequency_khz = kCenterFrequencyKhz,
          .frequency_khz = frequency_khz,
          .frequency_divider = candidate_divider,
      };
    }
  }

  return result;
}

DpllOscillatorConfig CreateDpllOscillatorConfigForDisplayPortTigerLake(int32_t afe_clock_khz) {
  ZX_ASSERT(afe_clock_khz > 0);

  DpllOscillatorConfig result = CreateDpllOscillatorConfigForHdmiTigerLake(afe_clock_khz);

  // These are the only cases where the HDMI algorithm deviates from the
  // DisplayPort table.
  if (afe_clock_khz == 1'350'000 || afe_clock_khz == 810'000 || afe_clock_khz == 1'620'000) {
    result.frequency_khz = 8'100'000;
    ZX_DEBUG_ASSERT(result.frequency_khz % afe_clock_khz == 0);
    result.frequency_divider = static_cast<int8_t>(result.frequency_khz / afe_clock_khz);
  }

  return result;
}

DpllFrequencyDividerConfig CreateDpllFrequencyDividerConfigKabyLake(int8_t dco_divider) {
  // The implementation conceptually follows the `getMultiplier()` function in
  // the "Pseudocode to Find HDMI and DVI DPLL Programming" section in the
  // display engine PRMs.
  //
  // Kaby Lake: IHD-OS-KBL-Vol 12-1.17 pages 135-136
  // Skylake: IHD-OS-SKL-Vol 12-05.16 pages 132-133

  if (dco_divider % 2 == 0) {
    const int8_t dco_divider_half = static_cast<int8_t>(dco_divider / 2);

    // The pseudocode has one if whose predicate is a big "or" clause comparing
    // the half-divider with all valid P2 (K) divider values. The loop below is
    // equivalent.
    static constexpr int8_t kP2DividerValues[] = {1, 2, 3, 5};
    for (const int8_t& p2_divider : kP2DividerValues) {
      if (dco_divider_half == p2_divider) {
        return {.p0_p_divider = 2, .p1_q_divider = 1, .p2_k_divider = dco_divider_half};
      }
    }

    // The pseudocode has a few if branches checking if the half-divider is
    // evenly divided by any valid P0 (P) divider values. The loop below is
    // equivalent.
    static constexpr int8_t kP0DividerValues[] = {2, 3, 7};
    for (const int8_t& p0_divider : kP0DividerValues) {
      if ((dco_divider_half % p0_divider) == 0) {
        return {.p0_p_divider = p0_divider,
                .p1_q_divider = static_cast<int8_t>(dco_divider_half / p0_divider),
                .p2_k_divider = 2};
      }
    }
    ZX_ASSERT_MSG(false, "Unhandled divider %d", dco_divider);
  }

  if (dco_divider == 3 || dco_divider == 9) {
    return {
        .p0_p_divider = 3, .p1_q_divider = 1, .p2_k_divider = static_cast<int8_t>(dco_divider / 3)};
  }
  // The pseudocode uses the P0 (P) divider for 5 and 7. That is incorrect,
  // because the P0 divider can only do 1/2/3/7.
  //
  // Taking a step back, there is a single solution that meets all the (P, Q, K)
  // constraints for all odd dividers that include 5 or 7 in their prime factor
  // decomposition. Q must be 1 because we can't set K to 2. So the 5 / 7 prime
  // factor must be set in P / K.
  if (dco_divider == 5 || dco_divider == 15 || dco_divider == 35) {
    return {
        .p0_p_divider = static_cast<int8_t>(dco_divider / 5), .p1_q_divider = 1, .p2_k_divider = 5};
  }
  if (dco_divider == 7 || dco_divider == 21) {
    return {
        .p0_p_divider = 7, .p1_q_divider = 1, .p2_k_divider = static_cast<int8_t>(dco_divider / 7)};
  }
  ZX_ASSERT_MSG(false, "Unhandled divider %d", dco_divider);
}

DpllFrequencyDividerConfig CreateDpllFrequencyDividerConfigTigerLake(int8_t dco_divider) {
  // The implementation conceptually follows the "Good divider found" block in
  // the "Pseudo-code for HDMI Mode DPLL Programming" section in the display
  // engine PRMs.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev2.0 pages 181-182

  if (dco_divider % 2 == 0) {
    const int8_t dco_divider_half = static_cast<int8_t>(dco_divider / 2);

    if (dco_divider == 2) {
      return {.p0_p_divider = 2, .p1_q_divider = 1, .p2_k_divider = 1};
    }

    // The pseudocode has a few if branches checking for valid P0 (P) divider
    // values. The comparisons check the divider directly against P0 values, or
    // against 2x the P0 (P) divider values. The difference only matters for
    // P0 = 2.
    //
    // The loop below is equivalent. It uses Kaby Lake / Skylake PRM approach of
    // checking the half-divider against P0 (P) values directly, which is
    // clearer.
    static constexpr int8_t kP0DividerValues[] = {2, 3, 5, 7};
    for (const int8_t& p0_divider : kP0DividerValues) {
      if ((dco_divider_half % p0_divider) == 0) {
        return {.p0_p_divider = p0_divider,
                .p1_q_divider = static_cast<int8_t>(dco_divider_half / p0_divider),
                .p2_k_divider = 2};
      }
    }

    ZX_ASSERT_MSG(false, "Unhandled divider %d", dco_divider);
  }

  if (dco_divider == 3 || dco_divider == 5 || dco_divider == 7) {
    return {.p0_p_divider = dco_divider, .p1_q_divider = 1, .p2_k_divider = 1};
  }
  ZX_ASSERT_MSG(dco_divider % 3 == 0, "Unhandled divider %d", dco_divider);
  return {
      .p0_p_divider = static_cast<int8_t>(dco_divider / 3), .p1_q_divider = 1, .p2_k_divider = 3};
}

}  // namespace i915_tgl
