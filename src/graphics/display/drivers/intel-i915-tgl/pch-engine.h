// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_PCH_ENGINE_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_PCH_ENGINE_H_

#include <lib/mmio/mmio-buffer.h>

#include <cstdint>

#include "src/graphics/display/drivers/intel-i915-tgl/registers-pch.h"

namespace i915_tgl {

// PCH display engine clocking.
//
// These values must be set during the display engine initialization sequence.
struct PchClockParameters {
  // Frequency for the PCH display engine's root clock.
  //
  // Zero can be configured, but is not a valid configuration value. Negative
  // values cannot be configured. The known values are 19.2, 24, and 38.4 MHz.
  //
  // The largest value that can be configured is 1,024.875 MHz if following the
  // documentation, or 1,031 MHz if documentated invariants are broken. The full
  // range fits in 30 bits.
  int32_t raw_clock_hz;

  // Frequency for the clock used by the PCH panel power sequences.
  //
  // Zero can be configured, but it suggests a misconfigured system. Negative
  // values cannot be configured. This is 10 kHz on all known systems.
  //
  // The largest value that can be configured is 512.4375Mhz if following the
  // documentation, or 515Mhz if documented invariants are broken. The full
  // range fits in 29 bits.
  //
  // This clock is not explicitly mentioned anywhere in the PRM. We inferred its
  // existence based on the description of the PP_DIVISOR register.
  int32_t panel_power_clock_hz;
};

bool operator==(const PchClockParameters& lhs, const PchClockParameters& rhs) noexcept;
inline bool operator!=(const PchClockParameters& lhs, const PchClockParameters& rhs) noexcept {
  return !(lhs == rhs);
}

// Characteristic parameters for the panel controlled by the PCH.
//
// The settings here only depend on the panel attached to the PCH control pins.
// Once set, they will not change for the lifetime of the driver.
//
// eDP timings are described in the eDP Standard version 1.4b (revised on
// December 31, 2020), Section 11 "Power Sequencing", pages 249 and 251.
struct PchPanelParameters {
  // Adjusts parameters that are obviously incorrect to safe values.
  //
  // The safe values may be sub-optimal. For example, panel delays may be longer
  // than necessary, resulting in slightly slower boot time.
  void Fix();

  // The eDP T3 delay, in microseconds.
  //
  // This is the delay expected by the PCH from the moment the panel power rail
  // goes above 90% to the moment the panel drives its HPD (Hot-Plug Detect) pin
  // high. The eDP specification states that the panel's AUX channel must be
  // ready to accept transactions as soon as its HPD pin is asserted high.
  //
  // Zero can be configured. Negative values cannot be configured. Typical
  // values are in the range of tens of milliseconds (10,000 us).
  int64_t power_on_to_hpd_aux_ready_delay_micros;

  // The eDP T2 delay, in microseconds.
  //
  // After turning on the panel power, the PCH will wait for T3 and this delay
  // before it enables the backlight.
  //
  // Intel's documentation is a bit unclear here. We currently assume this delay
  // is set to eDP T2 - the minimum delay from enabling panel power to Automatic
  // Black Video Generation, where the panel renders black video instead of
  // noise when it gets an invalid video signal.
  //
  // Zero can be configured. Negative values cannot be configured. Typical
  // values are in the range of hundreds of milliseconds (100,000 us).
  int64_t power_on_to_backlight_on_delay_micros;

  // The eDP T9 delay, in microseconds.
  //
  // This is the minimum delay needed by the panel from the moment the backlight
  // power is turned off to the moment the video signal stops being valid.
  //
  // Zero can be configured. Negative values cannot be configured. Typical
  // values are in the range of hundreds of milliseconds (100,000 us).
  //
  // eDP's T9 matches the SWPG standard's T6.
  int64_t backlight_off_to_video_end_delay_micros;

  // The eDP T10 delay, in microseconds.
  //
  // This is the minimum delay needed by the panel from the moment the source
  // stops emitting a video to the moment the panel power rail goes below 90%.
  //
  // Zero can be configured. Negative values cannot be configured. Typical
  // values are in the range of hundreds of milliseconds (100,000 us).
  //
  // eDP's T10 matches the SWPG standard's T3.
  int64_t video_end_to_power_off_delay_micros;

  // The eDP T12 delay, in microseconds.
  //
  // This is the minimum delay needed by the panel from the moment the power
  // rail goes below 10% until the moment the power rail is raised again above
  // 10%. The PCH's panel power subsystem honors this delay, unless the driver
  // forces panel power on.
  //
  // Zero can be configured. Negative values cannot be configured. The largest
  // value that can be configured is 3 seconds (3,000,000 us).
  //
  // eDP's T12 matches the SWPG standard's T4.
  int64_t power_cycle_delay_micros;

  // The frequency of the brightness PWM (Pulse-Width Modulation) pin, in Hertz.
  //
  // Lower frequencies have an increased likelihood that users will perceive
  // panel flickering when the brightness is not 0% or 100%.
  //
  // The range of acceptable brightness PWM frequencies is usually included in
  // the panel's specifications. 200 Hz is a safe value for most panels.
  int32_t backlight_pwm_frequency_hz;

  // If true, the PCH will start the panel power down sequence when it is reset.
  // Intel's PRM recommends setting this to true.
  bool power_down_on_reset;

  // Inverts whether the backlight PWM active duty drives the PWM pin high/low.
  //
  // If false (default mapping), the backlight PWM pin is driven high when the
  // PWM is in active duty, and the pin is driven low when the PWM is inactive.
  //
  // If true (inverted mapping), the backlight PWM pin is driven low when the
  // PWM is in active duty, and the pin is driven high when the PWM is inactive.
  bool backlight_pwm_inverted;
};

bool operator==(const PchPanelParameters& lhs, const PchPanelParameters& rhs) noexcept;
inline bool operator!=(const PchPanelParameters& lhs, const PchPanelParameters& rhs) noexcept {
  return !(lhs == rhs);
}

// The target configuration of the PCH panel power subsystem.
//
// The PCH may need some time to get the PCH panel to the target.
struct PchPanelPowerTarget {
  // If true, the PCH will (eventually) power on the panel. If false, the PCH
  // will (eventually) power off the panel.
  bool power_on;

  // If true, the PCH will turn on the panel backlight when the panel is powered
  // on. If false, the PCH will always keep the panel backlight off.
  bool backlight_on;

  // If true, the panel power subsystem is bypassed, and the panel VDD rail is
  // powered. If false, the panel's VDD rail is set by the panel power
  // subsystem, which follows the panel power on and off sequences.
  //
  // This mode can be used to perform transactions over the Embedded DisplayPort
  // AUX channel without executing the full panel power on sequence, which
  // requires configuring the panel power sequence delays, and setting up some
  // display engine resources.
  //
  // A call to SetPanelPowerTarget() with `force_power_on` = false must not be
  // followed by a call to SetForcePanelPowerOn() with `force_power_on` = true
  // within the eDP T12 delay. Otherwise, the panel may be damaged.
  //
  // Some Intel FSPs (Firmware Support Packages) ship with a default
  // configuration that enables this mode on boot. We turn off the override as
  // soon as it's safe to enable the panel power subsystem.
  bool force_power_on;

  // If true, the backlight brightness PWM (Pulse-Width Modulation) pin signals
  // the configured brightness level at the configured frequency. If false, the
  // backlight brightness PWM is never active. `PchPanelParameters` controls the
  // mapping between the PWM active/inactive states and the PWM pin states.
  //
  // The PWM counter should be disabled while `backlight_on` is false, to reduce
  // power consumption. If the PWM counter is disabled while the `backlight_on`
  // is true, the panel should act as if the backlight is off.
  bool brightness_pwm_counter_on;
};

bool operator==(const PchPanelPowerTarget& lhs, const PchPanelPowerTarget& rhs) noexcept;
inline bool operator!=(const PchPanelPowerTarget& lhs, const PchPanelPowerTarget& rhs) noexcept {
  return !(lhs == rhs);
}

// The state of the PCH panel power sequence subsystem.
//
// `kPoweredUp` and `kPoweredDown` are stable states.
//
// Setting the PCH panel power target to "on" will drive the panel through a
// subset of the following states:
// * `kPoweringDown` (if the power target was recently set to "off") ->
// * `kPoweredDown` ->
// * `kWaitingForPowerCycleDelay` (if the panel was recently powered off) ->
// * `kPoweringUp` ->
// * `kPoweredUp` - the target state.
//
// Setting the PCH panel power target to "off" will drive the panel through a
// subset of the following states:
// * `kPoweringUp` (if the power target was recently set to "on") ->
// * `kPoweredUp` ->
// * `kPoweringDown` ->
// * `kPoweredDown` - the target state.
enum class PchPanelPowerState : int {
  // The panel is powered down. This is a steady state.
  kPoweredDown = 0,

  // The panel was recently powered down.
  //
  // The PCH is planning to perform the panel power up sequence, but needs to
  // wait for the power cycle delay first.
  //
  // Both the eDP and SPWG Notebook Panel standards specify upper bounds on the
  // time a panel needs to power up. In practice, we may need to wait for
  // significantly longer times for panels to power up.
  kWaitingForPowerCycleDelay = 1,

  // The PCH is performing the panel power up sequence.
  //
  // Once the power up sequence starts, it must be completed. So, powering down
  // the panel may need to wait for the power up sequence to complete.
  kPoweringUp = 2,

  // The panel is powered up. This is a steady state.
  kPoweredUp = 3,

  // The PCH is performing the panel power down sequence.
  //
  // Once the power down sequence starts, it must be completed. So, powering up
  // the panel may need to wait for the power down sequence to complete, and
  // then wait for the power cycle delay.
  kPoweringDown = 4,
};

// Drives the display engine logic in the PCH (Platform Controller Hub).
//
// Intel's documentation also refers to this logic as the South Display Engine.
// This name was carried over from the Intel Hub Architecture, which had a
// Northbridge, which hosted the North Display Engine, and a Southbridge.
class PchEngine {
 public:
  // `mmio_buffer` must outlive this instance.
  PchEngine(fdf::MmioBuffer* mmio_buffer, int device_id);

  PchEngine(const PchEngine&) = delete;
  PchEngine(PchEngine&&) = delete;
  PchEngine& operator=(const PchEngine&) = delete;
  PchEngine& operator=(PchEngine&&) = delete;

  // Trivially destructible.
  ~PchEngine() = default;

  // If `enabled` is true, the north (main) display engine notifies the PCH
  // display engine of resets, and waits for it to acknowledge.
  //
  // This method must be called with `enabled` set to true during the cold-boot
  // display engine initialization sequence.
  void SetPchResetHandshake(bool enabled);

  // Overwrites the PCH clocking registers with cached values.
  //
  // This method performs MMIO writes unconditionally. It must only be called
  // during the display engine initialization sequence, when resuming from a
  // low-power (suspended) state.
  void RestoreClockParameters();

  // Overwrites most PCH configuration registers with cached values.
  //
  // This method restores all PCH configuration registers, *except* for the
  // registers covered by RestoreClockParameters(). This separation is needed to
  // comply with the mode set sequences documented by the Intel PRMs.
  //
  // This method performs MMIO writes unconditionally. It must only be called
  // when resuming from a low-power (suspended) state, after the display engine
  // is re-initialized. In particular, RestoreClockParameters() must have been
  // already called.
  //
  // Calling this method will restore the PCH to the configuration it had before
  // entering a low-power (suspended) state, with the following exceptions:
  // * The panel will be powered off, awaiting pipe and transcoder
  //   configuration.
  // * The backlight PWM will be disabled, since the panel is powered off.
  void RestoreNonClockParameters();

  // Reports the current PCH clocking configuration.
  //
  // This method is intended for retrieving the configuration applied by the
  // boot firmware. SetClockParameters() can perform any needed adjustments.
  PchClockParameters ClockParameters() const;

  // Updates the PCH clocking configuration.
  //
  // No MMIO writes are performed if `parameters` already matches the clocking
  // configuration.
  void SetClockParameters(const PchClockParameters& parameters);

  // Fixes clocking parameters that are obviously incorrect.
  void FixClockParameters(PchClockParameters& parameters) const;

  // Reports the current PCH panel configuration.
  //
  // This method is intended for retrieving the configuration applied by the
  // boot firmware. SetPanelParameters() can perform any needed adjustments.
  //
  // The caller should ensure that the PCH clocking is configured correctly
  // before calling this method. The result is not meaningful if the PCH
  // clocking is incorrect.
  PchPanelParameters PanelParameters() const;

  // Updates the PCH panel configuration.
  //
  // The caller must ensure that the PCH clocks are configured correctly before
  // calling this method.
  //
  // This method preserves (modulo precision errors) the PWM backlight's
  // brightness level when the PWM frequency changes. The brightness level will
  // be set to 0% if the PWM was not previously configured. The brightness level
  // will be normalized to 100% if it was (incorrectly) set above 100%.
  //
  // No MMIO writes are performed if `parameters` already matches the panel
  // configuration (unless the PWM brightness level must be normalized).
  void SetPanelParameters(const PchPanelParameters& parameters);

  // Reports the target configuration of the PCH panel power subsystem.
  //
  // This method is intended for retrieving the configuration applied by the
  // boot firmware. SetPanelPowerTarget() can drive the transition to new power
  // states.
  PchPanelPowerTarget PanelPowerTarget() const;

  // Returns the panel power state reported by the PCH.
  //
  // This method is not idempotent.
  PchPanelPowerState PanelPowerState();

  // Waits for the PCH panel power sequence to reach a given state.
  //
  // Returns true if the PCH panel reached the given state within the allotted
  // time. Returns false if the timeout ran out before the PCH panel reached the
  // desired state.
  //
  // While `power_state` can be any value, the meaningful values are kPoweredUp
  // and kPoweredDown.
  //
  // `timeout_us` must be positive. The eDP 1.4 standard allows for 90ms. The
  // SPWG Notebook Panel standard allows for 210ms. The Atlas panel needs almost
  // 400ms.
  bool WaitForPanelPowerState(PchPanelPowerState power_state, int timeout_us);

  // Updates the PCH panel power subsystem's target configuration.
  //
  // The caller must ensure that the PCH panel parameters are configured
  // correctly before calling this method with `power_on` set to true.
  // The caller must ensure that the PCH brightness PWM is configured correctly
  // before calling this method with `backlight_on` set to true.
  //
  // No MMIO writes are performed if `power_target` already matches the panel
  // power subsystem's target.
  void SetPanelPowerTarget(const PchPanelPowerTarget& power_target);

  // The brightness level created by the PCH panel backlight PWM.
  //
  // Returns a value between 0.0 (no brightness) and 1.0 (maximum brightness).
  double PanelBrightness() const;

  // Sets the brightness level created by the PCH panel backlight PWM.
  //
  // `brightness` must be between 0.0 (no brightness) and 1.0 (maximum
  // brightness).
  //
  // The caller must ensure that the PCH backlight brightness PWM is configured
  // correctly before calling this method.
  void SetPanelBrightness(double brightness);

  void Log();

 private:
  // ClockParameters() subset used by other functions. May return zero.
  int32_t RawClockHz() const;
  // ClockParameters() subset used by other functions. May return zero.
  int32_t PanelPowerClockHz() const;

  // SetClockParameters() helper that covers the raw clock.
  void SetRawClockHz(int32_t raw_clock_hz);
  // SetClockParameters() helper that covers the panel power sequence clock.
  // This must only be called after the raw clock was configured correctly.
  void SetPanelPowerClockHz(int32_t panel_power_clock_hz);

  // SetPanelParameters() helper that covers power sequence delays.
  void SetPanelPowerSequenceParameters(const PchPanelParameters& parameters);
  // SetPanelParameters() helper that covers the backlight PWM.
  void SetPanelBacklightPwmParameters(const PchPanelParameters& parameters);
  // Kaby Lake-specific logic for configuring the backlight PWM.
  // If the PWM frequency is changed, the PWM will be disabled first. The caller
  // is responsible for re-enabling the PWM.
  void SetPanelBacklightPwmParametersKabyLake(const PchPanelParameters& parameters);
  // Tiger Lake-specific logic for configuring the backlight PWM.
  // If the PWM frequency is changed, the PWM will be disabled first. The caller
  // is responsible for re-enabling the PWM.
  void SetPanelBacklightPwmParametersTigerLake(const PchPanelParameters& parameters);

  fdf::MmioBuffer* const mmio_buffer_;

  // GPU device ID used throughout the driver. Not the PCH's device ID.
  const int device_id_;

  tgl_registers::PchRawClock clock_;
  tgl_registers::PchChicken1 misc_;
  tgl_registers::PchBacklightFreq backlight_pwm_freq_;
  tgl_registers::PchBacklightDuty backlight_pwm_duty_;
  tgl_registers::PchBacklightFreqDuty backlight_freq_duty_;
  tgl_registers::PchBacklightControl backlight_control_;
  tgl_registers::PchPanelPowerOnDelays panel_power_on_delays_;
  tgl_registers::PchPanelPowerOffDelays panel_power_off_delays_;
  tgl_registers::PchPanelPowerClockDelay panel_power_clock_delay_;
  tgl_registers::PchPanelPowerControl panel_power_control_;
};

}  // namespace i915_tgl

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_PCH_ENGINE_H_
