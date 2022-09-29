// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_PCH_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_PCH_H_

// The registers in this file should only be accessed from the PchEngine class.

#include <cstdint>

#include <hwreg/bitfields.h>

namespace tgl_registers {

// NDE_RSTWRN_OPT (North Display Reset Warn Options)
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 134
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page 141
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 440
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 436
//
// This register has at least 1 bit that is reserved but not MBZ. The only safe
// way to modify it is via quick read-modify-write operations.
class DisplayResetOptions : public hwreg::RegisterBase<DisplayResetOptions, uint32_t> {
 public:
  // If true, the North Display Engine will notify PCH display engine when it is
  // reset, and will wait for the PCH display engine to acknowledge the reset.
  //
  // The display engine initialization sequence involves setting this to true.
  DEF_BIT(4, pch_reset_handshake);

  static auto Get() { return hwreg::RegisterAddr<DisplayResetOptions>(0x46408); }
};

// SBLC_PWM_CTL1 (South / PCH Backlight Control 1)
//
// Not directly documented for DG1, but mentioned in IHD-OS-DG1-Vol 12-2.21
// "Backlight Enabling Sequence" page 349.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 1154
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 787
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 772
class PchBacklightControl : public hwreg::RegisterBase<PchBacklightControl, uint32_t> {
 public:
  // Enables the PWM counter logic. When disabled, the PWM is always inactive.
  DEF_BIT(31, pwm_counter_enabled);

  DEF_RSVDZ_BIT(30);

  // Inverts whether the backlight PWM active duty drives the PWM pin high/low.
  //
  // When 0 (default), the backlight PWM pin is driven high when the PWM is in
  // active duty, and the pin is driven low when the PWM is inactive.
  //
  // When 1 (inverted), the backlight PWM pin is driven low when the PWM is in
  // active duty, and the pin is driven high when the PWM is inactive.
  DEF_BIT(29, pwm_polarity_inverted);

  DEF_RSVDZ_FIELD(28, 0);

  static auto Get() { return hwreg::RegisterAddr<PchBacklightControl>(0xc8250); }

  // Tiger Lake has another instance for a 2nd backlight at 0xc8350.
};

// SBLC_PWM_CTL2 (South / PCH Backlight Control 2)
//
// Does not exist on DG1 or Tiger Lake. The MMIO address is recycled for the new
// SLBC_PWM_FREQ register. The PWM frequency and duty cycle are specified in the
// SLBC_PWM_FREQ and SLBC_PWM_DUTY registers.
//
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 788
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 773
class PchBacklightFreqDuty : public hwreg::RegisterBase<PchBacklightFreqDuty, uint32_t> {
 public:
  // Based on the frequency of the clock and desired PWM frequency.
  //
  // PWM frequency = RAWCLK_FREQ / (freq_divider * backlight_pwm_multiplier)
  // backlight_pwm_multiplier is 16 or 128, based on SCHICKEN_1.
  DEF_FIELD(31, 16, freq_divider);

  // Must be <= `freq_divider`.
  // 0 = 0% PWM duty cycle. `freq_divider` = 100% PWM duty cycle.
  DEF_FIELD(15, 0, duty_cycle);

  static auto Get() { return hwreg::RegisterAddr<PchBacklightFreqDuty>(0xc8254); }
};

// SLBC_PWM_FREQ (South / PCH Backlight PWM Frequency)
//
// Does not exist on Kaby Lake and Skylake. PWM frequency and duty cycle are
// specified in the SBLC_PWM_CTL2 register.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 1156
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page 1205
class PchBacklightFreq : public hwreg::RegisterBase<PchBacklightFreq, uint32_t> {
 public:
  // (Reference clock frequency from RAWCLK_FREQ) / (Desired PWM frequency).
  DEF_FIELD(31, 0, divider);

  static auto Get() { return hwreg::RegisterAddr<PchBacklightFreq>(0xc8254); }

  // Tiger Lake has another instance for a 2nd backlight at 0xc8354.
};

// SBLC_PWM_DUTY (South / PCH Backlight PWM Duty Cycle)
//
// Does not exist on Kaby Lake and Skylake. PWM frequency and duty cycle are
// specified in the SBLC_PWM_CTL2 register.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 1155
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page 1205
class PchBacklightDuty : public hwreg::RegisterBase<PchBacklightDuty, uint32_t> {
 public:
  // Specified a scale from 0 (0%) to SBLC_PWM_FREQ (100%).
  // Must not exceed SBLC_PWM_FREQ.
  DEF_FIELD(31, 0, value);

  static auto Get() { return hwreg::RegisterAddr<PchBacklightDuty>(0xc8258); }

  // Tiger Lake has another instance for a 2nd backlight at 0xc8358.
};

// SCHICKEN_1 (South / PCH Display Engine Chicken 1)
//
// This register is not explicitly documented, but the Kaby Lake PRMs have clues
// that reveal its name and address.
// * IHD-OS-KBL-Vol 2c-1.17 Part 2 page 788 mentions the SCHICKEN_1 name, and
//   that bit 0 selects between a multiplier of 16 and 128 for SBLC_PWM_CTL2
//   backlight modulation and duty cycle.
// * IHD-OS-KBL-Vol 12-1.17 "Backlight Enabling Sequence" page 197 states that
//   a granularity of 16 or 128 is set in bit 0 of the 0xC2000 register.
//
// The name is a reference to "chicken bits", which are configuration bits that
// create the option to reverse (chicken out of) risky design changes (fixes or
// new features). The risk can be due to the complexity of the feature, or due
// to having to make changes late in the design cycle. More details in
// "Formal Verification - An Essential Toolkit for Modern VLSI Design".
class PchChicken1 : public hwreg::RegisterBase<PchChicken1, uint32_t> {
 public:
  // All bits must be set to 1 on DG1.
  //
  // Setting the bits to 1 compensates for the fact that DG1's HPD signals are
  // inverted (0 = connected, 1 = disconnected). This issue is not mentioned in
  // other PRMs.
  //
  // DG1: IHD-OS-DG1-Vol 12-2.21 "Hotplug Board Inversion" page 352 and
  //      IHD-OS-DG1-Vol 2c-2.21 Part 2 page 1259
  DEF_FIELD(18, 15, hpd_invert_bits);

  // Set on S0ix entry and cleared on S0ix exit.
  //
  // This bit works around an issue bug where the PCH display engine's clock
  // is not stopped when entering the S0ix state. This issue is mentioned in the
  // PRMs listed below.
  //
  // Lakefield: IHD-OS-LKF-Vol 14-4.21 page 15
  // Tiger Lake: IHD-OS-TGL-Vol 14-12.21 page 18 and page 50
  // Ice Lake: IHD-OS-ICLLP-Vol 14-1.20 page 33
  // Not mentioned in DG1, Kaby Lake, or Skylake.
  DEF_BIT(7, pch_display_clock_disable);

  // Toggles shared IO pins between multi-chip genlock and backlight 2.
  //
  // Lake Field: IHD-OS-LKF-Vol 12-4.21 page 50
  // DG1: IHD-OS-DG1-Vol 12-2.21 page 349
  // Kaby Lake and Skylake don't support multi-chip genlock.
  DEF_BIT(2, genlock_instead_of_backlight);

  // Multiplier for the backlight PWM frequency and duty cycle.
  //
  // This multiplier applies to SBLC_PWM_CTL1 and SBLC_PWM_CTL2. It is not
  // present on DG1, where the PWM frequency and duty cycle are specified as
  // 32-bit values in the SBLC_PWM_FREQ and SBLC_PWM_DUTY registers.
  //
  // The multiplier can be 16 (0) or 128 (1).
  //
  // Kaby Lake: IHD-OS-KBL-Vol 12-1.17 "Backlight Enabling Sequence" page 197
  // Skylake: IHD-OS-SKL-Vol 12-05.16 "Backlight Enabling Sequence" page 189
  // Does not exist on DG1.
  DEF_BIT(0, backlight_pwm_multiplier);

  static auto Get() { return hwreg::RegisterAddr<PchChicken1>(0xc2000); }
};

// RAWCLK_FREQ (Rawclk frequency)
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 1083-1084
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 1131-1132
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 712
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 705
class PchRawClock : public hwreg::RegisterBase<PchRawClock, uint32_t> {
 public:
  // The raw clock frequency in MHz. Complex representation used by DG1.
  //
  // Raw clock frequency = integral frequency + fractional frequency
  // Integral frequency = `integer` + 1
  // Fractional frequency = `fraction_numerator` / (`fraction_denominator` + 1)
  //
  // `fraction_denominator` must be zero if `fraction_numerator` is zero.
  // Only `fraction_numerator` values 0-2 are documented as supported.
  //
  // All these fields must be zero on Kaby Lake and Skylake.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 1083-1084
  // DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 1131-1132
  DEF_FIELD(29, 26, fraction_denominator);
  DEF_FIELD(25, 16, integer);
  DEF_FIELD(13, 11, fraction_numerator);

  // The raw clock frequency in MHz.
  //
  // This must be set to 24MHz on Kaby Lake and Skylake. Must be zero on Tiger
  // Lake and DG1.
  //
  // Kaby Lake: IHD-OS-KBL-Vol 12-1.17 page 195
  // Skylake: IHD-OS-SKL-Vol 12-05.16 page 188
  DEF_FIELD(9, 0, mhz);

  static auto Get() { return hwreg::RegisterAddr<PchRawClock>(0xc6204); }
};

// PP_CONTROL (Panel Power Control)
//
// The Tiger Lake PRMS do not include a description for this register. However,
// IHD-OS-TGL-Vol 14-12.21 pages 29 and 56 mention the register name and
// address. Experiments on Tiger Lake (device ID 0x9a49) suggest that this
// register has the same semantics as in DG1.
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 961-962
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 pages 986-987
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 pages 626-627
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 pages 620-621
class PchPanelPowerControl : public hwreg::RegisterBase<PchPanelPowerControl, uint32_t> {
 public:
  // eDP T12 - Required delay from panel power disable to power enable.
  //
  // Value = (desired_delay / 100ms) + 1.
  // Zero means no delay, and also stops a current delay.
  //
  // Must be zero on Kaby Lake and Skylake.
  DEF_FIELD(8, 4, power_cycle_delay);

  // If true, the eDP port's VDD is on even if the panel power sequence hasn't
  // been completed. Intended for panels that need VDD for DP AUX transactions.
  //
  // This setting overrides all power sequencing logic. So, we (the display
  // driver) must enforce the eDP T12 power delay. In other words, we must make
  // sure that that the delay between setting `force` to false and setting it
  // back to true is at least T12. Additional documentation sources:
  // * Kaby Lake - IHD-OS-KBL-Vol 16-1.17 page 20
  // * Skyake - IHD-OS-SKL-Vol 16-05.16 page 9
  //
  // The Intel documentation references the T4 delay from the SPWG Notebook
  // Panel Specification 3.8, Section 5.9 "Panel Power Sequence", page 26. The
  // T4 delay there is equivalent to the T12 delay in the eDP Standard version
  // 1.4b (revised on December 31, 2020), Section 11 "Power Sequencing", pages
  // 249 and 251.
  DEF_BIT(3, vdd_always_on);

  // If true, the backlight is on when the panel is in the powered on state.
  DEF_BIT(2, backlight_enabled);

  // If true, panel runs power down sequence when reset is detected.
  //
  // Recommended for preserving the panel's lifetime.
  DEF_BIT(1, power_down_on_reset);

  // If true, the panel will eventually be powered on. This may initiate a panel
  // power on sequence, which would require waiting for an ongoing power off
  // sequence to complete, and then honoring the T12 delay.
  //
  // If false, the panel will eventually be powered off. This may initiate a
  // power off sequence, which would require waiting for an ongoing power on
  // sequence to complete, and then honoring the TXX delay.
  //
  // The panel power on sequence must not be initiated until all panel delays
  // are set correctly.
  DEF_BIT(0, power_state_target);

  static auto Get() { return hwreg::RegisterAddr<PchPanelPowerControl>(0xc7204); }

  // Tiger Lake has another instance for a 2nd panel at 0xc7304.
};

// PP_DIVISOR (Panel Power Cycle Delay and Reference Divisor)
//
// On Tiger Lake and DG1, the T12 value is stored in PP_CONTROL, and there is no
// documented register for setting the panel clock divisor.
//
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 629
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 623
class PchPanelPowerClockDelay : public hwreg::RegisterBase<PchPanelPowerClockDelay, uint32_t> {
 public:
  // Divider that generates the panel power clock from the PCH raw clock.
  //
  // Value = divider / 2 - 1. 0 is not a valid value.
  //
  // Intel's PRMs state that the panel clock must always be 10 kHz. This results
  // in a 100us period, which is assumed to be the base unit for all panel
  // timings.
  DEF_FIELD(31, 8, clock_divider);

  // eDP T12 - Required delay from panel power disable to power enable.
  //
  // Value = (desired_delay / 100ms) + 1.
  // Zero means no delay, and also stops a current delay.
  //
  // This field is stored in PP_CONTROL on DG1.
  DEF_FIELD(4, 0, power_cycle_delay);

  static auto Get() { return hwreg::RegisterAddr<PchPanelPowerClockDelay>(0xc7210); }
};

// PP_OFF_DELAYS (Panel Power Off Sequencing Delays)
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 963
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page 988
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 629
// Skylake: IHD-OS-SKL-Vol 2c-05.16 Part 2 page 623
class PchPanelPowerOffDelays : public hwreg::RegisterBase<PchPanelPowerOffDelays, uint32_t> {
 public:
  DEF_RSVDZ_FIELD(31, 29);

  // eDP T10 - Minimum delay from valid video data end to panel power disabled.
  // eDP T10 = register value * 100us.
  DEF_FIELD(28, 16, video_end_to_power_off_delay);

  DEF_RSVDZ_FIELD(15, 13);

  // eDP T9 - Minimum delay from backlight disabled to valid video data end.
  // eDP T9 = register value * 100us.
  DEF_FIELD(12, 0, backlight_off_to_video_end_delay);

  static auto Get() { return hwreg::RegisterAddr<PchPanelPowerOffDelays>(0xc720c); }

  // Tiger Lake has another instance for a 2nd panel at 0xc730c.
};

// PP_ON_DELAYS (Panel Power On Sequencing Delays)
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 page 964
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page 989
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 page 630
// Skylake:  IHD-OS-SKL-Vol 2c-05.16 Part 2 page 624
class PchPanelPowerOnDelays : public hwreg::RegisterBase<PchPanelPowerOnDelays, uint32_t> {
 public:
  DEF_RSVDZ_FIELD(31, 29);

  // eDP T3 - Expected delay from enabling panel power to HPD and AUX ready.
  // eDP T3 = register value * 100us.
  DEF_FIELD(28, 16, power_on_to_hpd_aux_ready_delay);

  DEF_RSVDZ_FIELD(15, 13);

  // Minimum delay from power on until the backlight can be enabled.
  // The PCH will not enable the backlight until T3 and this delay have passed.
  // Delay = register value * 100us.
  //
  // This seems to match eDP T2 - the minimum delay from enabling panel
  // power to Automatic Black Video Generation, where the panel renders black
  // video instead of noise when it gets an invalid video signal.
  DEF_FIELD(12, 0, power_on_to_backlight_on_delay);

  static auto Get() { return hwreg::RegisterAddr<PchPanelPowerOnDelays>(0xc7208); }

  // Tiger Lake has another instance for a 2nd panel at 0xc7308.
};

// PP_STATUS (Panel Power Status)
//
// Tiger Lake: IHD-OS-TGL-Vol 2c-1.22-Rev2.0 Part 2 pages 965-966
// DG1: IHD-OS-DG1-Vol 2c-2.21 Part 2 page 990
// Kaby Lake: IHD-OS-KBL-Vol 2c-1.17 Part 2 pages 631-632
// Skylake:  IHD-OS-SKL-Vol 2c-05.16 Part 2 page 625
class PchPanelPowerStatus : public hwreg::RegisterBase<PchPanelPowerStatus, uint32_t> {
 public:
  enum class Transition : int {
    kNone = 0,          // Not in a power sequence.
    kPoweringUp = 1,    // Powering up, or waiting for T12 delay.
    kPoweringDown = 2,  // Powering down.
    kInvalid = 3,       // Reserved value.
  };

  // If true, the panel is powered up. It may be powering down.
  // If false, the panel is powered down. A T12 delay may be in effect.
  DEF_BIT(31, panel_on);

  DEF_RSVDZ_BIT(30);

  DEF_FIELD(29, 28, power_transition_bits);
  Transition PowerTransition() const { return static_cast<Transition>(power_transition_bits()); }

  // If true, a T12 delay (power down to power up) is in effect.
  DEF_BIT(27, power_cycle_delay_active);

  DEF_RSVDZ_FIELD(26, 4);

  static auto Get() { return hwreg::RegisterAddr<PchPanelPowerStatus>(0xc7200); }

  // Tiger Lake has another instance for a 2nd panel at 0xc7300.
};

}  // namespace tgl_registers

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_REGISTERS_PCH_H_
