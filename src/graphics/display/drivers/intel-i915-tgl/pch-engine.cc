// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/pch-engine.h"

#include <lib/ddk/debug.h>
#include <lib/mmio/mmio-buffer.h>
#include <lib/zx/time.h>
#include <zircon/assert.h>

#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <tuple>

#include "src/graphics/display/drivers/intel-i915-tgl/pci-ids.h"
#include "src/graphics/display/drivers/intel-i915-tgl/poll-until.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-ddi.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-pch.h"

namespace i915_tgl {

namespace {

// The frequency of the (inferred) PCH clock used for panel power sequencing.
//
// This is the value requested in the Kaby Lake and Skylake PRMs. The register
// reference (Vol 2c) in the Tiger Lake and DG1 PRMs mention the same
// resolution, but doesn't describe any method for changing it.
//
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 629
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 623
constexpr int32_t kPrescribedPanelPowerClockHz = 10'000;

}  // namespace

bool operator==(const PchClockParameters& lhs, const PchClockParameters& rhs) noexcept {
  return std::make_tuple(lhs.raw_clock_hz, lhs.panel_power_clock_hz) ==
         std::make_tuple(rhs.raw_clock_hz, rhs.panel_power_clock_hz);
}

void PchPanelParameters::Fix() {
  if (power_cycle_delay_micros == 0) {
    // Maximum values based on eDP and SPWG Notebook Panel standards.
    power_cycle_delay_micros = 500'000;

    // eDP T1+T3 max.
    if (power_on_to_hpd_aux_ready_delay_micros == 0) {
      power_on_to_hpd_aux_ready_delay_micros = 90'000;
    }

    // SPWG T1+T2+T5 max/min.
    if (power_on_to_backlight_on_delay_micros == 0) {
      power_on_to_backlight_on_delay_micros = 260'000;
    }

    // SPWG T6 min
    if (backlight_off_to_video_end_delay_micros == 0) {
      backlight_off_to_video_end_delay_micros = 200'000;
    }

    // eDP T10 max
    if (video_end_to_power_off_delay_micros == 0) {
      video_end_to_power_off_delay_micros = 500'000;
    }
  }

  if (backlight_pwm_frequency_hz < 1'000) {
    backlight_pwm_frequency_hz = 1'000;
  }

  // Always recommended.
  power_down_on_reset = true;
}

bool operator==(const PchPanelParameters& lhs, const PchPanelParameters& rhs) noexcept {
  return std::make_tuple(
             lhs.power_on_to_hpd_aux_ready_delay_micros, lhs.power_on_to_backlight_on_delay_micros,
             lhs.backlight_off_to_video_end_delay_micros, lhs.video_end_to_power_off_delay_micros,
             lhs.power_cycle_delay_micros, lhs.backlight_pwm_frequency_hz, lhs.power_down_on_reset,
             lhs.backlight_pwm_inverted) ==
         std::make_tuple(
             rhs.power_on_to_hpd_aux_ready_delay_micros, rhs.power_on_to_backlight_on_delay_micros,
             rhs.backlight_off_to_video_end_delay_micros, rhs.video_end_to_power_off_delay_micros,
             rhs.power_cycle_delay_micros, rhs.backlight_pwm_frequency_hz, rhs.power_down_on_reset,
             rhs.backlight_pwm_inverted);
}

bool operator==(const PchPanelPowerTarget& lhs, const PchPanelPowerTarget& rhs) noexcept {
  return std::make_tuple(lhs.power_on, lhs.backlight_on, lhs.force_power_on,
                         lhs.brightness_pwm_counter_on) ==
         std::make_tuple(rhs.power_on, rhs.backlight_on, rhs.force_power_on,
                         rhs.brightness_pwm_counter_on);
}

PchEngine::PchEngine(fdf::MmioBuffer* mmio_buffer, int device_id)
    : mmio_buffer_(mmio_buffer), device_id_(device_id) {
  ZX_DEBUG_ASSERT(mmio_buffer);

  // Register reads are ordered by MMIO address. There are no other ordering
  // requirements, and this ordering might have a slight performance advantage,
  // if the range is prefetchable.

  misc_ = tgl_registers::PchChicken1::Get().ReadFrom(mmio_buffer);
  clock_ = tgl_registers::PchRawClock::Get().ReadFrom(mmio_buffer);

  panel_power_control_ = tgl_registers::PchPanelPowerControl::Get().ReadFrom(mmio_buffer);
  panel_power_on_delays_ = tgl_registers::PchPanelPowerOnDelays::Get().ReadFrom(mmio_buffer);
  panel_power_off_delays_ = tgl_registers::PchPanelPowerOffDelays::Get().ReadFrom(mmio_buffer);
  if (is_skl(device_id) || is_kbl(device_id)) {
    panel_power_clock_delay_ = tgl_registers::PchPanelPowerClockDelay::Get().ReadFrom(mmio_buffer);
  }

  backlight_control_ = tgl_registers::PchBacklightControl::Get().ReadFrom(mmio_buffer);
  if (is_skl(device_id) || is_kbl(device_id)) {
    backlight_freq_duty_ = tgl_registers::PchBacklightFreqDuty::Get().ReadFrom(mmio_buffer);
  }
  if (is_tgl(device_id)) {
    backlight_pwm_freq_ = tgl_registers::PchBacklightFreq::Get().ReadFrom(mmio_buffer);
    backlight_pwm_duty_ = tgl_registers::PchBacklightDuty::Get().ReadFrom(mmio_buffer);
  }
}

void PchEngine::SetPchResetHandshake(bool enabled) {
  auto display_reset_options = tgl_registers::DisplayResetOptions::Get().ReadFrom(mmio_buffer_);
  if (display_reset_options.pch_reset_handshake() == enabled)
    return;
  display_reset_options.set_pch_reset_handshake(enabled).WriteTo(mmio_buffer_);
}

void PchEngine::RestoreClockParameters() {
  clock_.WriteTo(mmio_buffer_);
  if (is_skl(device_id_) || is_kbl(device_id_)) {
    panel_power_clock_delay_.WriteTo(mmio_buffer_);
  }

  if (is_tgl(device_id_)) {
    // The restore side of the workaround for the PCH display engine clock
    // remaining enabled during suspend. The PRM documents two version of the
    // workaround. We implement the version that resets the
    // `pch_display_clock_disable` field during restore, because this version is
    // resilient to the boot firmware changing the field.
    //
    // Lakefield: IHD-OS-LKF-Vol 14-4.21 page 15
    // Tiger Lake: IHD-OS-TGL-Vol 14-12.21 page 18 and page 50
    // Ice Lake: IHD-OS-ICLLP-Vol 14-1.20 page 33
    misc_.set_pch_display_clock_disable(0);
  }
  // The workaround above suggests that the `misc` register may re-enable the
  // PCH display engine clock. To be safe, we restore it after restoring the
  // clock configuration registers.
  misc_.WriteTo(mmio_buffer_);
}

void PchEngine::RestoreNonClockParameters() {
  // At this stage, the panel must remain powered down, and the brightness PWM
  // must be disabled. The pipes and transcoders are not yet restored. Later in
  // the recovery process, the panel and brightness will be restored, if
  // necessary.
  panel_power_control_.set_power_state_target(0).set_backlight_enabled(0);
  backlight_control_.set_pwm_counter_enabled(0);

  panel_power_on_delays_.WriteTo(mmio_buffer_);
  panel_power_off_delays_.WriteTo(mmio_buffer_);

  // The panel power sequence delays must be configured before turning on the
  // panel. This requirement is met if we restore `panel_control_` after
  // restoring all the other registers that configure the panel power sequence.
  //
  // On Kaby Lake and Skylake, the dependencies include the `misc_` and
  // `panel_power_clock_delay_` registers. These registers are handled by
  // RestoreClockParameters(), which must have been called earlier.
  //
  // Writing to `panel_control_` is currently guaranteed not to turn on
  // the panel. Our restore code will continue working if this changes.
  panel_power_control_.WriteTo(mmio_buffer_);

  if (is_skl(device_id_) || is_kbl(device_id_)) {
    backlight_freq_duty_.WriteTo(mmio_buffer_);
  }
  if (is_tgl(device_id_)) {
    backlight_pwm_freq_.WriteTo(mmio_buffer_);
    backlight_pwm_duty_.WriteTo(mmio_buffer_);
  }

  // The brightness PWM frequency and duty cycle must be configured before
  // enabling the PWM. This requirement is met if we restore
  // `backlight_control_` after restoring all other backlight PWM registers.
  //
  // Writing to `backlight_control_` is currently guaranteed not to enable the
  // PWM. Our restore code will continue working if this changes.
  backlight_control_.WriteTo(mmio_buffer_);
}

PchPanelPowerState PchEngine::PanelPowerState() {
  auto status = tgl_registers::PchPanelPowerStatus::Get().ReadFrom(mmio_buffer_);

  auto power_transition = status.PowerTransition();
  if (power_transition == tgl_registers::PchPanelPowerStatus::Transition::kPoweringDown) {
    // According to Intel's PRM, status.panel_on() should be 1.
    return PchPanelPowerState::kPoweringDown;
  }

  if (power_transition == tgl_registers::PchPanelPowerStatus::Transition::kPoweringUp) {
    // According to Intel's PRM, status.panel_on() should be 0.

    // The power up sequence includes waiting for a T12 (power cycle) delay.
    if (status.power_cycle_delay_active())
      return PchPanelPowerState::kWaitingForPowerCycleDelay;
    return PchPanelPowerState::kPoweringUp;
  }

  if (status.panel_on())
    return PchPanelPowerState::kPoweredUp;

  if (status.power_cycle_delay_active())
    return PchPanelPowerState::kWaitingForPowerCycleDelay;

  return PchPanelPowerState::kPoweredDown;
}

bool PchEngine::WaitForPanelPowerState(PchPanelPowerState power_state, int timeout_us) {
  ZX_ASSERT(timeout_us > 0);

  // Typical timeout values are hundreds of ms. A granularity of 10ms strikes a
  // decent balance between unnecessarily waiting, and taking the CPU away from
  // other tasks.
  static constexpr int wait_granularity_us = 10'000;
  static constexpr zx::duration wait_granularity = zx::usec(wait_granularity_us);

  // The subtraction and division are safe because `wait_granularity_us` is
  // guaranteed to be non-negative.
  int poll_intervals = (timeout_us + wait_granularity_us - 1) / wait_granularity_us;
  return PollUntil([&] { return PanelPowerState() == power_state; }, wait_granularity,
                   poll_intervals);
}

PchClockParameters PchEngine::ClockParameters() const {
  return PchClockParameters{
      .raw_clock_hz = RawClockHz(),
      .panel_power_clock_hz = PanelPowerClockHz(),
  };
}

void PchEngine::SetClockParameters(const PchClockParameters& parameters) {
  SetRawClockHz(parameters.raw_clock_hz);
  SetPanelPowerClockHz(parameters.panel_power_clock_hz);
}

void PchEngine::FixClockParameters(PchClockParameters& parameters) const {
  if (parameters.panel_power_clock_hz == 0) {
    // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 629
    // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 623
    //
    // On devices where the panel power sequencing clock is not configurable,
    // ClockParameters() returns the correct value.
    parameters.panel_power_clock_hz = kPrescribedPanelPowerClockHz;
  }

  if (is_skl(device_id_) || is_kbl(device_id_) || is_test_device(device_id_)) {
    // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 712
    // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 705

    if (parameters.raw_clock_hz == 0) {
      // The boot firmware should really have set the PCH raw clock. Use the
      // documented default.
      parameters.raw_clock_hz = 24'000'000;
    }
    return;
  }

  if (is_tgl(device_id_)) {
    // IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 1185 and pages 1083-1084

    auto pch_fuses = tgl_registers::PchDisplayFuses::Get().ReadFrom(mmio_buffer_);
    parameters.raw_clock_hz = pch_fuses.rawclk_is_24mhz() ? 24'000'000 : 19'200'000;
    return;
  }

  ZX_ASSERT_MSG(false, "Unsupported PCI device ID %d", device_id_);
}

int32_t PchEngine::RawClockHz() const {
  if (is_skl(device_id_) || is_kbl(device_id_)) {
    // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 712
    // Skylake:  IHD-OS-SKL-Vol 2c-05.16 Part 2 page 705

    // The cast does not cause UB, because mhz() is a 10-bit field.
    //
    // For the same reason, the maximum configurable frequency is 1023MHz, which
    // fits in 30 bits when expressed in Hertz. So, the multiplication is
    // guaranteed not to overflow (which would cause UB).
    return static_cast<int32_t>(clock_.mhz()) * 1'000'000;
  }

  if (is_tgl(device_id_)) {
    // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 1083-1084
    // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 1131-1132
    //
    // The Tiger Lake and DG1 PRMs document different patterns for the
    // integer part of the frequency (the microsecond counter divider). The
    // Tiger Lake manual suggests that the integer is stored exactly as-is. The
    // DG1 manual suggests that the integer field stores the real value - 1.
    //
    // The production Tiger Lake devices we've encountered (NUC11, Dell 5420)
    // use the approach documented in the DG1 manual.

    // The cast does not cause UB, because integer() is a 10-bit field.
    //
    // For the same reason, the maximum configurable frequency is 1024MHz, which
    // fits in 30 bits when expressed in Hertz. So, the multiplication is
    // guaranteed not to overflow (which would cause UB).
    const int32_t integer = (static_cast<int32_t>(clock_.integer()) + 1) * 1'000'000;

    // The cast does not cause UB because fraction_numerator() is a 3-bit field.
    //
    // For the same reason, the maximum configurable numerator is 7, which fits
    // in 13 bits when expressed in Hertz. So, the multiplication is guaranteed
    // not to overflow (which would cause UB).
    const int32_t numerator = static_cast<int32_t>(clock_.fraction_numerator()) * 1'000'000;

    // The cast does not cause UB, because fraction_denominator() is a 4-bit field.
    //
    // For the same reason, range of configurable denominators is 1-16. So, the
    // addition is guaranteed not to overflow (which would cause UB).
    const int32_t denominator = static_cast<int32_t>(clock_.fraction_denominator()) + 1;

    // The division does not cause UB, because the denominator is >= 1.
    //
    // The range of results is from 0 (0 / 1) to 7,000,000 (7,000,000 / 1). So,
    // the division is guaranteed not to overflow (which would cause UB).
    const int32_t fraction = numerator / denominator;

    // The maximum addition result is 1,031,000,000 which fits in 31 bits. So,
    // the addition will not overflow.
    return integer + fraction;
  }

  if (is_test_device(device_id_)) {
    return 24'000'000;  // Kaby Lake default raw clock.
  }

  ZX_ASSERT_MSG(false, "Unsupported PCI device ID %d", device_id_);
  return 0;
}

void PchEngine::SetRawClockHz(int32_t raw_clock_hz) {
  ZX_ASSERT(raw_clock_hz >= 1'000'000);

  const uint32_t old_clock = clock_.reg_value();

  if (is_skl(device_id_) || is_kbl(device_id_)) {
    // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 712
    // Skylake:  IHD-OS-SKL-Vol 2c-05.16 Part 2 page 705

    // `mhz` is a 10-bit field.
    static constexpr int32_t kMaxRawMhz = (1 << 10) - 1;
    const int32_t raw_mhz = std::min(raw_clock_hz / 1'000'000, kMaxRawMhz);

    clock_.set_mhz(raw_mhz);
  } else if (is_tgl(device_id_)) {
    // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 1083-1084
    // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 1131-1132
    //
    // The Tiger Lake code uses the relationships suggested in the DG1 manual.
    // See the RawClockHz() comments for a detailed justification.

    // `integer` is a 10-bit field.
    static constexpr int32_t kMaxRawInteger = (1 << 10) - 1;
    // The subtraction result is non-negative, because `raw_clock_hz` is at
    // least 1,000,000, so the division result is at least 1.
    const int32_t raw_integer = std::min(raw_clock_hz / 1'000'000 - 1, kMaxRawInteger);

    const int32_t target_fraction_hz = raw_clock_hz % 1'000'000;

    // Find the `numerator` and `denominator` that yield a fraction closest to
    // the target fraction. The first guess is 0 / 1.
    int32_t raw_numerator = 0, raw_denominator = 0;

    // std::abs(0 - target_fraction_hz) is `target_fraction_hz`.
    int32_t min_diff_hz = target_fraction_hz;

    static constexpr int32_t kMaxNumerator = (1 << 3) - 1;  // 3-bit field
    static constexpr int32_t kMaxDenominator = (1 << 4);    // 4-bit field, offset by 1
    for (int32_t numerator = 1; numerator <= kMaxNumerator; ++numerator) {
      // The multiplication will not overflow (causing UB) because `numerator`
      // is a 3-bit unsigned integer. So the result is at most 7,000,000.
      const int32_t numerator_hz = numerator * 1'000'000;

      // The fraction must always be less than 1.
      for (int32_t denominator = numerator + 1; denominator <= kMaxDenominator; ++denominator) {
        const int32_t fraction_hz = numerator_hz / denominator;

        // The subtraction result will not overflow 32 bits (causing UB) because
        // `fraction_hz` is between 0 and 7,000,000 and `target_fraction_hz` is
        // between 0 and 1,000,000.
        const int32_t diff_hz = std::abs(fraction_hz - target_fraction_hz);

        if (diff_hz < min_diff_hz) {
          min_diff_hz = diff_hz;
          raw_numerator = numerator;
          raw_denominator = denominator - 1;
        }
      }
    }

    clock_.set_integer(raw_integer)
        .set_fraction_numerator(raw_numerator)
        .set_fraction_denominator(raw_denominator);
  } else if (is_test_device(device_id_)) {
    // Stubbed out for integration tests.
  } else {
    ZX_ASSERT_MSG(false, "Unsupported PCI device ID %d", device_id_);
  }

  if (clock_.reg_value() != old_clock) {
    clock_.WriteTo(mmio_buffer_);
  }
}

int32_t PchEngine::PanelPowerClockHz() const {
  if (is_skl(device_id_) || is_kbl(device_id_)) {
    // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 629
    // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 623

    // The cast does not cause UB because clock_divider() is a 24-bit field.
    const int32_t raw_divider = static_cast<int32_t>(panel_power_clock_delay_.clock_divider());

    // clock_divider() is a 24-bit field, so the addition result will fit in 25
    // bits, and the multiplication result will fit in 26 bits. So, the
    // multiplication is guaranteed not to overflow (causing UB).
    const int32_t divider = (raw_divider + 1) * 2;

    // The division will not cause UB because divider is >= 2. The division
    // result is guaranteed to be non-negative, because both operands are
    // non-negative.
    //
    // The maximum result (configurable value) is 515Mhz. The maximum result
    // without breaking documented invariants is 512.4375Mhz.
    return RawClockHz() / divider;
  }

  if (is_tgl(device_id_)) {
    // No documented register for changing the panel power clock divider on
    // Tiger Lake. The clock should always be set to 10kHz.
    return kPrescribedPanelPowerClockHz;
  }

  if (is_test_device(device_id_)) {
    return kPrescribedPanelPowerClockHz;
  }

  ZX_ASSERT_MSG(false, "Unsupported PCI device ID %d", device_id_);
  return kPrescribedPanelPowerClockHz;
}

void PchEngine::SetPanelPowerClockHz(int32_t panel_power_clock_hz) {
  ZX_ASSERT(panel_power_clock_hz > 0);

  if (is_skl(device_id_) || is_kbl(device_id_)) {
    // Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 629
    // Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 623

    // The division will not cause UB because `panel_power_clock_hz` must be
    // non-negative. The division result is non-negative because both inputs are
    // non-negative.
    const int32_t divider = RawClockHz() / panel_power_clock_hz;

    // `clock_divider` is a 24-bit field and must not be set to zero.
    static constexpr int32_t kMaxRawDivider = (1 << 24) - 1;
    // The subtraction result fits in 32 bits (will not cause UB) because the
    // left-hand side is the result of a division by 2, so its range is at most
    // half of the range of int32_t.
    const int32_t raw_divider = std::max(std::min(divider / 2 - 1, kMaxRawDivider), 1);

    const uint32_t old_panel_power_clock_delay = panel_power_clock_delay_.reg_value();
    panel_power_clock_delay_.set_clock_divider(raw_divider);
    if (panel_power_clock_delay_.reg_value() != old_panel_power_clock_delay) {
      panel_power_clock_delay_.WriteTo(mmio_buffer_);
    }
    return;
  }

  if (is_tgl(device_id_)) {
    // No documented register for changing the panel power clock divider on
    // Tiger Lake. The clock should always be set to 10kHz.
    return;
  }

  if (is_test_device(device_id_)) {
    // Stubbed out for integration tests.
    return;
  }
  ZX_ASSERT_MSG(false, "Unsupported PCI device ID %d", device_id_);
}

PchPanelParameters PchEngine::PanelParameters() const {
  // Return zeros instead of crashing if the PCH is not clocked correctly. This
  // lets us log the PCH configuration even when it's invalid.
  const int32_t panel_power_clock_hz = PanelPowerClockHz();
  const int32_t multiplier = (panel_power_clock_hz == 0) ? 0 : 1'000'000 / panel_power_clock_hz;

  // The casts do not cause UB because the register fields are 13-bit values.
  const int32_t raw_power_on_to_hpd_aux_ready_delay =
      static_cast<int32_t>(panel_power_on_delays_.power_on_to_hpd_aux_ready_delay());
  const int32_t raw_power_on_to_backlight_on_delay =
      static_cast<int32_t>(panel_power_on_delays_.power_on_to_backlight_on_delay());
  const int32_t raw_backlight_off_to_video_end_delay =
      static_cast<int32_t>(panel_power_off_delays_.backlight_off_to_video_end_delay());
  const int32_t raw_video_end_to_power_off_delay =
      static_cast<int32_t>(panel_power_off_delays_.video_end_to_power_off_delay());

  int32_t raw_power_cycle_delay;
  uint32_t backlight_pwm_divider;
  if (is_skl(device_id_) || is_kbl(device_id_)) {
    // The cast is not UB because power_cycle_delay() is a 5-bit field.
    raw_power_cycle_delay = static_cast<int32_t>(panel_power_clock_delay_.power_cycle_delay());
    if (raw_power_cycle_delay > 1)
      raw_power_cycle_delay -= 1;

    const uint32_t pwm_divider_granularity = misc_.backlight_pwm_multiplier() ? 128 : 16;

    // The cast does not cause UB because freq_divider() is a 16-bit field. The
    // multiplication will not overflow (causing UB) because maximum result fits
    // in 23 bits (16-bit unsigned integer multiplied by 128).
    backlight_pwm_divider = backlight_freq_duty_.freq_divider() * pwm_divider_granularity;
  } else if (is_tgl(device_id_)) {
    // The cast does not cause UB because power_cycle_delay() is a 5-bit field.
    raw_power_cycle_delay = static_cast<int32_t>(panel_power_control_.power_cycle_delay());
    if (raw_power_cycle_delay > 1)
      raw_power_cycle_delay -= 1;

    // The cast does not cause UB because freq_divider() is a 32-bit field.
    backlight_pwm_divider = static_cast<uint32_t>(backlight_pwm_freq_.divider());
  } else if (is_test_device(device_id_)) {
    raw_power_cycle_delay = 0;
    backlight_pwm_divider = 0;
  } else {
    ZX_ASSERT_MSG(false, "Unsupported PCI device ID %d", device_id_);
  }

  // The cast does not cause UB because RawClockHz() fits in 30 bits.
  const uint32_t raw_clock_hz = static_cast<uint32_t>(RawClockHz());

  const int32_t backlight_pwm_frequency_hz =
      // The multiplication will not overflow (causing UB) because
      // `raw_clock_hz` fits in 30 bits.
      (backlight_pwm_divider == 0 || raw_clock_hz * 2 < backlight_pwm_divider)
          ? 0
          // The golden results in the unit tests, which are lifted from the PRMs,
          // require rounding. The addition will not overflow (causing UB) because
          // `raw_clock_hz` fits in 30 bits, and `backlight_pwm_divider` can be at
          // most twice as large.
          : (raw_clock_hz + backlight_pwm_divider / 2) / backlight_pwm_divider;

  return PchPanelParameters{
      // The maximum theoretical multiplication result is 8191
      // The multiplication results fit in 33 bits, because `multiplier` fits in
      // 20 bits, and the raw delay values fit in 13 bits.
      .power_on_to_hpd_aux_ready_delay_micros =
          raw_power_on_to_hpd_aux_ready_delay * static_cast<int64_t>(multiplier),
      .power_on_to_backlight_on_delay_micros =
          raw_power_on_to_backlight_on_delay * static_cast<int64_t>(multiplier),
      .backlight_off_to_video_end_delay_micros =
          raw_backlight_off_to_video_end_delay * static_cast<int64_t>(multiplier),
      .video_end_to_power_off_delay_micros =
          raw_video_end_to_power_off_delay * static_cast<int64_t>(multiplier),

      // The first multiplication result is at most 31,000 because
      // `raw_power_cycle_delay` fits in 5 bits. So, int32_t is sufficient for
      // the multiplication result, and no overflow (UB) will occur.
      //
      //
      // The second multiplication result fits in 35 bits, because `multiplier`
      // fits in 20 bits, and the first multiplication result fits in 15 bits.
      .power_cycle_delay_micros =
          static_cast<int64_t>(raw_power_cycle_delay * int32_t{1'000}) * multiplier,

      .backlight_pwm_frequency_hz = backlight_pwm_frequency_hz,

      // The casts are safe because they involve 1-bit fields.
      .power_down_on_reset = static_cast<bool>(panel_power_control_.power_down_on_reset()),
      .backlight_pwm_inverted = static_cast<bool>(backlight_control_.pwm_polarity_inverted()),
  };
}

void PchEngine::SetPanelParameters(const PchPanelParameters& parameters) {
  SetPanelPowerSequenceParameters(parameters);
  SetPanelBacklightPwmParameters(parameters);
}

void PchEngine::SetPanelPowerSequenceParameters(const PchPanelParameters& parameters) {
  ZX_ASSERT(parameters.power_on_to_hpd_aux_ready_delay_micros >= 0);
  ZX_ASSERT(parameters.power_on_to_backlight_on_delay_micros >= 0);
  ZX_ASSERT(parameters.backlight_off_to_video_end_delay_micros >= 0);
  ZX_ASSERT(parameters.video_end_to_power_off_delay_micros >= 0);
  ZX_ASSERT(parameters.power_cycle_delay_micros >= 0);

  const int32_t panel_power_clock_hz = PanelPowerClockHz();
  ZX_ASSERT_MSG(panel_power_clock_hz > 0, "PCH not clocked correctly");

  const int32_t power_delay_divider = 1'000'000 / panel_power_clock_hz;
  ZX_ASSERT_MSG(power_delay_divider > 0, "PCH not clocked correctly");

  const uint32_t old_power_on_delays = panel_power_on_delays_.reg_value();
  const uint32_t old_power_off_delays = panel_power_off_delays_.reg_value();

  // The raw delays are written into 13-bit register fields.
  constexpr int64_t kMaxRawDelay = (1 << 13) - 1;

  // The casts do not cause UB because the std::min() results fit in 13 bits.
  const int32_t raw_power_on_to_hpd_aux_ready_delay = static_cast<int32_t>(std::min(
      parameters.power_on_to_hpd_aux_ready_delay_micros / power_delay_divider, kMaxRawDelay));
  const int32_t raw_power_on_to_backlight_on_delay = static_cast<int32_t>(std::min(
      parameters.power_on_to_backlight_on_delay_micros / power_delay_divider, kMaxRawDelay));
  const int32_t raw_backlight_off_to_video_end_delay = static_cast<int32_t>(std::min(
      parameters.backlight_off_to_video_end_delay_micros / power_delay_divider, kMaxRawDelay));
  const int32_t raw_video_end_to_power_off_delay = static_cast<int32_t>(
      std::min(parameters.video_end_to_power_off_delay_micros / power_delay_divider, kMaxRawDelay));

  panel_power_on_delays_.set_power_on_to_hpd_aux_ready_delay(raw_power_on_to_hpd_aux_ready_delay)
      .set_power_on_to_backlight_on_delay(raw_power_on_to_backlight_on_delay);
  panel_power_off_delays_.set_backlight_off_to_video_end_delay(raw_backlight_off_to_video_end_delay)
      .set_video_end_to_power_off_delay(raw_video_end_to_power_off_delay);

  if (panel_power_on_delays_.reg_value() != old_power_on_delays) {
    panel_power_on_delays_.WriteTo(mmio_buffer_);
  }
  if (panel_power_off_delays_.reg_value() != old_power_off_delays) {
    panel_power_off_delays_.WriteTo(mmio_buffer_);
  }

  // This delay is written in a 5-bit register field.
  constexpr int64_t kMaxRawPowerCycleDelay = (1 << 5) - 1;

  // The multiplication will not overflow (causing UB), because
  // `power_delay_divider` fits in 20 bits. So, the multiplication result will
  // fit in 30 bits.
  const int32_t raw_power_delay_divider = power_delay_divider * 1'000;

  // The division is not UB because we ensure that `power_delay_divider` is
  // positive above. The addition will not overflow (causing UB), because the
  // previous division's result is at most 1,000 times less than the maximum
  // integer. The cast does not cause UB because the std::min() result fits in 5
  // bits.
  const int32_t raw_power_cycle_delay = static_cast<int32_t>(std::min(
      parameters.power_cycle_delay_micros / raw_power_delay_divider + 1, kMaxRawPowerCycleDelay));

  const uint32_t old_panel_power_control = panel_power_control_.reg_value();

  if (is_kbl(device_id_) || is_skl(device_id_)) {
    const uint32_t old_panel_power_clock_delay = panel_power_clock_delay_.reg_value();
    panel_power_clock_delay_.set_power_cycle_delay(raw_power_cycle_delay);
    if (panel_power_clock_delay_.reg_value() != old_panel_power_clock_delay) {
      panel_power_clock_delay_.WriteTo(mmio_buffer_);
    }
  } else if (is_tgl(device_id_)) {
    panel_power_control_.set_power_cycle_delay(raw_power_cycle_delay);
  } else if (is_test_device(device_id_)) {
    // Stubbed out for integration tests.
  } else {
    ZX_ASSERT_MSG(false, "Unsupported PCI device ID %d", device_id_);
  }

  panel_power_control_.set_power_down_on_reset(parameters.power_down_on_reset);
  if (panel_power_control_.reg_value() != old_panel_power_control) {
    panel_power_control_.WriteTo(mmio_buffer_);
  }
}

namespace {

// Computes the PWM duty cycle to be used with a new frequency.
//
// The return value is guaranteed to be <= `frequency_divider`.
//
// It's safe to pass un-validated register contents directly to this function.
// Returns zero (0% brightness) if `old_frequency_divider` is zero
// (un-configured PWM). Returns `frequency_divider` if `old_duty_cycle` exceeds
// `old_frequency_divider`, clamping the brightness to 100% in case the PWM is
// configured incorrectly.
//
// The arguments and return types must be uint32_t because some display engines
// (currently Tiger Lake and DG1) use 32-bit (unsigned) register fields to
// represent the frequency divider and duty cycle.
uint32_t ScaledPwmDutyCycle(uint32_t frequency_divider, uint32_t old_duty_cycle,
                            uint32_t old_frequency_divider) {
  if (old_frequency_divider == 0)
    return 0;

  // The multiplication will not overflow because both factors are 32-bit
  // integers.
  const uint64_t scaled_duty_cycle =
      (uint64_t{old_duty_cycle} * frequency_divider) / old_frequency_divider;

  // The cast is safe because std::min()'s result will be at most
  // `frequency_divider`, which fits in 32 bits.
  const uint32_t clamped_duty_cycle =
      static_cast<uint32_t>(std::min<uint64_t>(scaled_duty_cycle, frequency_divider));
  return clamped_duty_cycle;
}

}  // namespace

void PchEngine::SetPanelBacklightPwmParameters(const PchPanelParameters& parameters) {
  ZX_ASSERT(parameters.backlight_pwm_frequency_hz > 0);

  // This implements the sections "Panel Power and Backlight" > "Backlight
  // Enabling Sequence" and "Backlight Frequency Change Sequence" under  section
  // in the display engine PRMs.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev2.0 pages 426-427
  // DG1: IHD-OS-DG1-Vol 12-2.21 pages 349-350
  // Ice Lake: IHD-OS-ICLLP-Vol 12-1.22-Rev2.0 pages 370-371

  // The backlight PWM must be disabled while changing the PWM frequency. This
  // is not a theoretical issue -- we observed a panel whose backlight remains
  // off for minutes if we attempt to change the PWM frequency while the PWM
  // remains enabled. We also want to avoid disabling and re-enabling the PWM if
  // we're only going to change the duty cycle (brightness).
  //
  // To accomplish this, the SetPanelBacklightPwmParameters*() methods called
  // below disable the PWM if necessary. `old_backlight_control` captures the
  // PWM enablement state before we make any changes, so it is correctly
  // restored before the end of the method.
  //
  // Disabling the brightness PWM while the panel backlight is enabled is
  // supported, and results in well-defined behavior. The backlight goes to 100%
  // brightness. (As an aside, this seems like the best failure mode we could
  // have hoped for. A flicker of brightness seems better than a flicker of
  // complete darkness, which is the other plausible alternative.)
  const uint32_t old_backlight_control = backlight_control_.reg_value();

  if (is_kbl(device_id_) || is_skl(device_id_)) {
    SetPanelBacklightPwmParametersKabyLake(parameters);
  } else if (is_tgl(device_id_)) {
    SetPanelBacklightPwmParametersTigerLake(parameters);
  } else if (is_test_device(device_id_)) {
    // Stubbed out for integration tests.
    return;
  } else {
    ZX_ASSERT_MSG(false, "Unsupported PCI device ID %d", device_id_);
  }

  // `set_reg_value()` undoes any changes that SetPanelBacklightPwmParameters*()
  // might have applied.
  backlight_control_.set_reg_value(old_backlight_control)
      .set_pwm_polarity_inverted(parameters.backlight_pwm_inverted);
  if (backlight_control_.reg_value() != old_backlight_control) {
    backlight_control_.WriteTo(mmio_buffer_);
  }
}

void PchEngine::SetPanelBacklightPwmParametersKabyLake(const PchPanelParameters& parameters) {
  const int32_t pwm_divider_granularity = misc_.backlight_pwm_multiplier() ? 128 : 16;

  // std::min() explicitly clamps one of the multipliers so that the
  // multiplication will not overflow, which would cause UB.
  //
  // Clamping is sufficient (as opposed to using int64_t) because the dividend
  // is the PCH clock frequency, which fits in 30 bits.
  //
  // The result is positive because the caller ensures that
  // `backlight_pwm_frequency_hz` is positive.
  const int32_t pwm_divider =
      std::min(parameters.backlight_pwm_frequency_hz,
               std::numeric_limits<int32_t>::max() / pwm_divider_granularity) *
      pwm_divider_granularity;

  // The frequency divider and duty cycle are 16-bit fields.
  static constexpr int32_t kMaxRawField = (1 << 16) - 1;

  // The division will not cause UB because `pwm_divider` is positive. The Intel
  // PRMs don't explicitly state that the PWM frequency divider shouldn't be
  // zero. We assume this is a good idea.
  const int32_t new_frequency_divider =
      std::max(1, std::min(RawClockHz() / pwm_divider, kMaxRawField));
  ZX_DEBUG_ASSERT(new_frequency_divider > 0);
  const uint32_t raw_frequency_divider = static_cast<uint32_t>(new_frequency_divider);

  // The cast does not cause UB because `raw_frequncy_divider` fits in 16 bits.
  const uint32_t raw_duty_cycle =
      ScaledPwmDutyCycle(static_cast<uint32_t>(raw_frequency_divider),
                         backlight_freq_duty_.duty_cycle(), backlight_freq_duty_.freq_divider());
  ZX_ASSERT(raw_duty_cycle <= raw_frequency_divider);
  ZX_ASSERT(raw_duty_cycle <= kMaxRawField);  // Implied by the check above.

  const uint32_t old_backlight_freq_duty = backlight_freq_duty_.reg_value();
  if (backlight_freq_duty_.freq_divider() != raw_frequency_divider) {
    // The backlight PWM must be turned off while changing the frequency. The
    // SetPanelBacklightPwmParameters() implementation has a deeper explanation.
    if (backlight_control_.pwm_counter_enabled()) {
      backlight_control_.set_pwm_counter_enabled(false).WriteTo(mmio_buffer_);
    }
  }

  backlight_freq_duty_.set_freq_divider(raw_frequency_divider);
  backlight_freq_duty_.set_duty_cycle(raw_duty_cycle);
  if (backlight_freq_duty_.reg_value() != old_backlight_freq_duty) {
    backlight_freq_duty_.WriteTo(mmio_buffer_);
  }
}

void PchEngine::SetPanelBacklightPwmParametersTigerLake(const PchPanelParameters& parameters) {
  // The cast is safe because RawClockHz() is non-negative and fits in 32 bits,
  // so the division result will also fit in 32 bits.
  const uint32_t raw_frequency_divider =
      static_cast<uint32_t>(std::max(1, RawClockHz() / parameters.backlight_pwm_frequency_hz));

  // We use the logical values in diffing (instead of the raw register values)
  // because the logical values perfectly map to the register values.
  const uint32_t old_frequency_divider = backlight_pwm_freq_.divider();
  const uint32_t old_duty_cycle = backlight_pwm_duty_.value();

  if (old_frequency_divider != raw_frequency_divider) {
    // The backlight PWM must be turned off while changing the frequency. The
    // SetPanelBacklightPwmParameters() implementation has a deeper explanation.
    //
    // Doing this here means we don't need to worry about possibly (briefly)
    // breaking the invariant that the PWM duty cycle must not exceed the PWM
    // frequency divider.
    if (backlight_control_.pwm_counter_enabled()) {
      backlight_control_.set_pwm_counter_enabled(false).WriteTo(mmio_buffer_);
    }
    backlight_pwm_freq_.set_divider(raw_frequency_divider).WriteTo(mmio_buffer_);
  }

  const uint32_t raw_duty_cycle =
      ScaledPwmDutyCycle(raw_frequency_divider, old_duty_cycle, old_frequency_divider);
  ZX_ASSERT(raw_duty_cycle <= raw_frequency_divider);
  if (old_duty_cycle != raw_duty_cycle) {
    backlight_pwm_duty_.set_value(raw_duty_cycle).WriteTo(mmio_buffer_);
  }
}

PchPanelPowerTarget PchEngine::PanelPowerTarget() const {
  return PchPanelPowerTarget{
      // The casts are safe because these are all 1-bit fields.
      .power_on = static_cast<bool>(panel_power_control_.power_state_target()),
      .backlight_on = static_cast<bool>(panel_power_control_.backlight_enabled()),
      .force_power_on = static_cast<bool>(panel_power_control_.vdd_always_on()),
      .brightness_pwm_counter_on = static_cast<bool>(backlight_control_.pwm_counter_enabled()),
  };
}

void PchEngine::SetPanelPowerTarget(const PchPanelPowerTarget& power_target) {
  const uint32_t old_panel_power_control = panel_power_control_.reg_value();
  panel_power_control_.set_power_state_target(power_target.power_on)
      .set_backlight_enabled(power_target.backlight_on)
      .set_vdd_always_on(power_target.force_power_on);

  const uint32_t old_backlight_control = backlight_control_.reg_value();
  backlight_control_.set_pwm_counter_enabled(power_target.brightness_pwm_counter_on);

  if (panel_power_control_.reg_value() != old_panel_power_control) {
    panel_power_control_.WriteTo(mmio_buffer_);
  }
  if (backlight_control_.reg_value() != old_backlight_control) {
    backlight_control_.WriteTo(mmio_buffer_);
  }
}

double PchEngine::PanelBrightness() const {
  uint32_t pwm_duty;
  uint32_t pwm_freq_divider;

  if (is_skl(device_id_) || is_kbl(device_id_)) {
    pwm_duty = backlight_freq_duty_.duty_cycle();
    pwm_freq_divider = backlight_freq_duty_.freq_divider();
  } else if (is_tgl(device_id_)) {
    pwm_duty = backlight_pwm_duty_.value();
    pwm_freq_divider = backlight_pwm_freq_.divider();
  } else if (is_test_device(device_id_)) {
    pwm_duty = 0;
    pwm_freq_divider = 1;
  } else {
    ZX_ASSERT_MSG(false, "Unsupported PCI device ID %d", device_id_);
  }

  if (pwm_freq_divider == 0) {
    // This matches the brightness level "preserved" by SetPanelParameters().
    return 0;
  }

  ZX_ASSERT_MSG(pwm_duty <= pwm_freq_divider, "Brightness PWM is configured incorrectly");
  return static_cast<double>(pwm_duty) / static_cast<double>(pwm_freq_divider);
}

void PchEngine::SetPanelBrightness(double brightness) {
  ZX_ASSERT(brightness >= 0.0);
  ZX_ASSERT(brightness <= 1.0);

  if (is_skl(device_id_) || is_kbl(device_id_)) {
    // The cast is safe because freq_divider() is a 16-bit field.
    const int32_t pwm_freq_divider = static_cast<int32_t>(backlight_freq_duty_.freq_divider());
    if (pwm_freq_divider == 0) {
      return;
    }
    const int32_t pwm_duty = std::min(
        // The cast is not UB because `brightness` is between 0 and 1, so the
        // rounding result should be between 0 and `pwm_freq_divider`.
        static_cast<int32_t>(std::round(static_cast<double>(pwm_freq_divider) * brightness)),
        pwm_freq_divider);

    const uint32_t old_backlight_freq_duty = backlight_freq_duty_.reg_value();
    backlight_freq_duty_.set_duty_cycle(pwm_duty);
    if (backlight_freq_duty_.reg_value() != old_backlight_freq_duty) {
      backlight_freq_duty_.WriteTo(mmio_buffer_);
    }
    return;
  }

  if (is_tgl(device_id_)) {
    const uint32_t pwm_freq_divider = backlight_pwm_freq_.divider();
    if (pwm_freq_divider == 0) {
      return;
    }
    const uint32_t pwm_duty = std::min(
        // The cast to uint32_t is safe because `brightness` is between 0 and 1,
        // so the rounding result should be between 0 and `pwm_freq_divider`.
        static_cast<uint32_t>(std::round(static_cast<double>(pwm_freq_divider) * brightness)),
        pwm_freq_divider);

    // We use the logical value in diffing (instead of the raw register values)
    // because the logical value perfectly maps to the register value.
    if (pwm_duty != backlight_pwm_duty_.value()) {
      backlight_pwm_duty_.set_value(pwm_duty).WriteTo(mmio_buffer_);
    }
    return;
  }

  if (is_test_device(device_id_)) {
    return;  // Stubbed out for integration tests.
  }

  ZX_ASSERT_MSG(false, "Unsupported PCI device ID %d", device_id_);
}

void PchEngine::Log() {
  const PchClockParameters clock_parameters = ClockParameters();
  zxlogf(TRACE, "PCH Raw Clock: %d Hz", clock_parameters.raw_clock_hz);
  zxlogf(TRACE, "PCH Panel Power Clock frequency: %d Hz", clock_parameters.panel_power_clock_hz);

  const char* state_text = "bug";
  switch (PanelPowerState()) {
    case PchPanelPowerState::kPoweredDown:
      state_text = "powered down";
      break;
    case PchPanelPowerState::kWaitingForPowerCycleDelay:
      state_text = "power cycle delay";
      break;
    case PchPanelPowerState::kPoweringUp:
      state_text = "powering up";
      break;
    case PchPanelPowerState::kPoweredUp:
      state_text = "powered up";
      break;
    case PchPanelPowerState::kPoweringDown:
      state_text = "powering down";
      break;
  }
  zxlogf(TRACE, "PCH Panel power state: %s", state_text);

  const PchPanelPowerTarget power_target = PanelPowerTarget();
  zxlogf(TRACE, "PCH Panel power target: %s", power_target.power_on ? "on" : "off");
  zxlogf(TRACE, "PCH Panel backlight: %s", power_target.backlight_on ? "enabled" : "disabled");
  zxlogf(TRACE, "PCH Panel VDD operation: %s",
         power_target.force_power_on ? "forced on" : "standard");
  zxlogf(TRACE, "PCH Backlight counter %s",
         power_target.brightness_pwm_counter_on ? "enabled" : "disabled");

  const PchPanelParameters panel_parameters = PanelParameters();
  zxlogf(TRACE, "PCH Panel T2 delay: %" PRId64 " us",
         panel_parameters.power_on_to_backlight_on_delay_micros);
  zxlogf(TRACE, "PCH Panel T3 delay: %" PRId64 " us",
         panel_parameters.power_on_to_hpd_aux_ready_delay_micros);
  zxlogf(TRACE, "PCH Panel T9 delay: %" PRId64 " us",
         panel_parameters.backlight_off_to_video_end_delay_micros);
  zxlogf(TRACE, "PCH Panel T10 delay: %" PRId64 " us",
         panel_parameters.video_end_to_power_off_delay_micros);
  zxlogf(TRACE, "PCH Panel T12 delay: %" PRId64 " us", panel_parameters.power_cycle_delay_micros);
  zxlogf(TRACE, "PCH Panel power down on reset: %s",
         panel_parameters.power_down_on_reset ? "on" : "off");
  zxlogf(TRACE, "PCH Backlight PWM frequency: %" PRId32 " Hz",
         panel_parameters.backlight_pwm_frequency_hz);
  zxlogf(TRACE, "PCH Backlight PWM polarity: %s",
         panel_parameters.backlight_pwm_inverted ? "inverted" : "not inverted");

  zxlogf(TRACE, "NDE_RSTWRN_OPT: %" PRIx32,
         tgl_registers::DisplayResetOptions::Get().ReadFrom(mmio_buffer_).reg_value());
  zxlogf(TRACE, "SCHICKEN_1: %" PRIx32, misc_.reg_value());
  zxlogf(TRACE, "RAWCLK_FREQ: %" PRIx32, clock_.reg_value());

  zxlogf(TRACE, "PP_CONTROL: %" PRIx32, panel_power_control_.reg_value());
  zxlogf(TRACE, "PP_ON_DELAYS: %" PRIx32, panel_power_on_delays_.reg_value());
  zxlogf(TRACE, "PP_OFF_DELAYS: %" PRIx32, panel_power_off_delays_.reg_value());
  zxlogf(TRACE, "PP_STATUS: %" PRIx32,
         tgl_registers::PchPanelPowerStatus::Get().ReadFrom(mmio_buffer_).reg_value());
  if (is_skl(device_id_) || is_kbl(device_id_)) {
    zxlogf(TRACE, "PP_DIVISOR: %" PRIx32, panel_power_clock_delay_.reg_value());
  }

  zxlogf(TRACE, "SBLC_PWM_CTL1: %" PRIx32, backlight_control_.reg_value());
  if (is_skl(device_id_) || is_kbl(device_id_)) {
    zxlogf(TRACE, "SBLC_PWM_CTL2: %" PRIx32, backlight_freq_duty_.reg_value());
  }
  if (is_tgl(device_id_)) {
    zxlogf(TRACE, "SBLC_PWM_FREQ: %" PRIx32, backlight_pwm_freq_.reg_value());
    zxlogf(TRACE, "SBLC_PWM_DUTY: %" PRIx32, backlight_pwm_duty_.reg_value());
  }
}

}  // namespace i915_tgl
