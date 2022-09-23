// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/clock/cdclk.h"

#include <lib/ddk/debug.h>
#include <lib/zx/time.h>
#include <zircon/assert.h>

#include <cstdint>

#include "src/graphics/display/drivers/intel-i915-tgl/poll-until.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-dpll.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers.h"

namespace i915_tgl {

namespace {

struct GtDriverMailboxOp {
  uint32_t addr;
  uint64_t val;
  uint32_t poll_busy_timeout_us = 150;
  uint32_t poll_freq_us = 0;
  uint32_t total_timeout_us = 0;
};

bool WriteToGtMailbox(fdf::MmioBuffer* mmio_space, GtDriverMailboxOp op) {
  constexpr uint32_t kGtDriverMailboxInterface = 0x138124;
  constexpr uint32_t kGtDriverMailboxData0 = 0x138128;
  constexpr uint32_t kGtDriverMailboxData1 = 0x13812c;

  for (uint32_t total_wait_us = 0;;) {
    mmio_space->Write32(static_cast<uint32_t>(op.val & 0xffffffff), kGtDriverMailboxData0);
    mmio_space->Write32(static_cast<uint32_t>(op.val >> 32), kGtDriverMailboxData1);
    mmio_space->Write32(op.addr, kGtDriverMailboxInterface);

    if (op.total_timeout_us == 0 || op.poll_freq_us == 0) {
      return true;
    }

    if (!PollUntil(
            [&] { return (mmio_space->Read32(kGtDriverMailboxInterface) & 0x80000000) == 0; },
            zx::usec(1), static_cast<int32_t>(op.poll_busy_timeout_us))) {
      zxlogf(ERROR, "GT Driver Mailbox driver busy");
      return false;
    }

    if (PollUntil([&] { return (mmio_space->Read32(kGtDriverMailboxData0) & 0x1) != 0; },
                  zx::usec(1), static_cast<int32_t>(op.poll_freq_us))) {
      break;
    }

    total_wait_us += op.poll_freq_us;
    if (total_wait_us >= op.total_timeout_us) {
      zxlogf(ERROR, "GT Driver Mailbox: Write timeout");
      return false;
    }
  }
  return true;
}

}  // namespace

CoreDisplayClockSkylake::CoreDisplayClockSkylake(fdf::MmioBuffer* mmio_space)
    : mmio_space_(mmio_space) {
  bool current_freq_is_valid = LoadState();
  ZX_DEBUG_ASSERT(current_freq_is_valid);
}

bool CoreDisplayClockSkylake::LoadState() {
  auto dpll_enable =
      tgl_registers::DpllEnable::GetForSkylakeDpll(tgl_registers::DPLL_0).ReadFrom(mmio_space_);
  if (!dpll_enable.enable_dpll()) {
    zxlogf(ERROR, "Skylake CDCLK LoadState: DPLL0 is disabled");
    return false;
  }

  auto dpll_ctrl1 = tgl_registers::DpllControl1::Get().ReadFrom(mmio_space_);
  auto cdclk_ctl = tgl_registers::CdClockCtl::Get().ReadFrom(mmio_space_);
  auto skl_cd_freq_select = cdclk_ctl.skl_cd_freq_select();

  auto link_rate = dpll_ctrl1.GetLinkRate(tgl_registers::DPLL_0);
  bool is_vco_8640 = (link_rate == tgl_registers::DpllControl1::LinkRate::k1080Mhz) ||
                     (link_rate == tgl_registers::DpllControl1::LinkRate::k2160Mhz);

  switch (skl_cd_freq_select) {
    case tgl_registers::CdClockCtl::kFreqSelect3XX:
      set_current_freq_khz(is_vco_8640 ? 308'570 : 337'500);
      break;
    case tgl_registers::CdClockCtl::kFreqSelect4XX:
      set_current_freq_khz(is_vco_8640 ? 432'000 : 450'000);
      break;
    case tgl_registers::CdClockCtl::kFreqSelect540:
      set_current_freq_khz(540'000);
      break;
    case tgl_registers::CdClockCtl::kFreqSelect6XX:
      set_current_freq_khz(is_vco_8640 ? 617'140 : 675'000);
      break;
    default:
      zxlogf(ERROR, "Invalid CD Clock frequency");
      return false;
  }

  return true;
}

bool CoreDisplayClockSkylake::PreChangeFreq() {
  bool raise_voltage_result = WriteToGtMailbox(mmio_space_, {
                                                                .addr = 0x80000007,
                                                                .val = 0x3,
                                                                .poll_busy_timeout_us = 150,
                                                                .poll_freq_us = 150,
                                                                .total_timeout_us = 3000,
                                                            });
  if (!raise_voltage_result) {
    zxlogf(ERROR, "Set CDCLK: Failed to raise voltage to max level");
    return false;
  }
  return true;
}

bool CoreDisplayClockSkylake::PostChangeFreq(uint32_t freq_khz) {
  bool set_voltage_result = WriteToGtMailbox(mmio_space_, {
                                                              .addr = 0x80000007,
                                                              .val = FreqToVoltageLevel(freq_khz),
                                                              .poll_busy_timeout_us = 0,
                                                              .poll_freq_us = 0,
                                                              .total_timeout_us = 0,
                                                          });
  if (!set_voltage_result) {
    zxlogf(ERROR, "Set CDCLK: Failed to set voltage");
    return false;
  }
  return true;
}

bool CoreDisplayClockSkylake::CheckFrequency(uint32_t freq_khz) {
  auto dpll_enable =
      tgl_registers::DpllEnable::GetForSkylakeDpll(tgl_registers::DPLL_0).ReadFrom(mmio_space_);
  if (!dpll_enable.enable_dpll()) {
    zxlogf(ERROR, "Skylake CDCLK CheckFrequency: DPLL0 is disabled");
    return false;
  }

  auto dpll_ctrl1 = tgl_registers::DpllControl1::Get().ReadFrom(mmio_space_);
  auto link_rate = dpll_ctrl1.GetLinkRate(tgl_registers::DPLL_0);
  bool is_vco_8640 = link_rate == tgl_registers::DpllControl1::LinkRate::k1080Mhz ||
                     link_rate == tgl_registers::DpllControl1::LinkRate::k2160Mhz;
  if (is_vco_8640) {
    // VCO 8640
    return freq_khz == 308'570 || freq_khz == 432'000 || freq_khz == 540'000 || freq_khz == 617'140;
  }
  // VCO 8100
  return freq_khz == 337'500 || freq_khz == 450'000 || freq_khz == 540'000 || freq_khz == 675'000;
}

bool CoreDisplayClockSkylake::ChangeFreq(uint32_t freq_khz) {
  // Set the cd_clk frequency to |freq_khz|.
  auto cd_clk = tgl_registers::CdClockCtl::Get().ReadFrom(mmio_space_);
  switch (freq_khz) {
    case 308'570:
    case 337'500:
      cd_clk.set_skl_cd_freq_select(tgl_registers::CdClockCtl::kFreqSelect3XX);
      break;
    case 432'000:
    case 450'000:
      cd_clk.set_skl_cd_freq_select(tgl_registers::CdClockCtl::kFreqSelect4XX);
      break;
    case 540'000:
      cd_clk.set_skl_cd_freq_select(tgl_registers::CdClockCtl::kFreqSelect540);
      break;
    case 617'140:
    case 675'000:
      cd_clk.set_skl_cd_freq_select(tgl_registers::CdClockCtl::kFreqSelect6XX);
      break;
    default:
      // Unreachable
      ZX_DEBUG_ASSERT(false);
      return false;
  }
  cd_clk.set_cd_freq_decimal(tgl_registers::CdClockCtl::FreqDecimal(freq_khz));
  cd_clk.WriteTo(mmio_space_);
  return true;
}

bool CoreDisplayClockSkylake::SetFrequency(uint32_t freq_khz) {
  if (!CheckFrequency(freq_khz)) {
    zxlogf(ERROR, "Skylake CDCLK ChangeFreq: Invalid frequency %u KHz", freq_khz);
    return false;
  }

  // Changing CD Clock Frequency specified on
  // intel-gfx-prm-osrc-skl-vol12-display.pdf p.135-136.
  if (!PreChangeFreq()) {
    return false;
  }
  if (!ChangeFreq(freq_khz)) {
    return false;
  }
  if (!PostChangeFreq(freq_khz)) {
    return false;
  }
  set_current_freq_khz(freq_khz);
  return true;
}

// static
uint32_t CoreDisplayClockSkylake::FreqToVoltageLevel(uint32_t freq_khz) {
  if (freq_khz > 540'000) {
    return 0x3;
  }
  if (freq_khz > 450'000) {
    return 0x2;
  }
  if (freq_khz > 337'500) {
    return 0x1;
  }
  return 0x0;
}

// Tiger Lake

CoreDisplayClockTigerLake::CoreDisplayClockTigerLake(fdf::MmioBuffer* mmio_space)
    : mmio_space_(mmio_space) {
  bool load_state_result = LoadState();
  ZX_DEBUG_ASSERT(load_state_result);
}

bool CoreDisplayClockTigerLake::LoadState() {
  // Load ref clock frequency.
  auto dssm = tgl_registers::Dssm::Get().ReadFrom(mmio_space_);
  switch (dssm.GetRefFrequency()) {
    case tgl_registers::Dssm::RefFrequency::k19_2Mhz:
      ref_clock_khz_ = 19'200;
      break;
    case tgl_registers::Dssm::RefFrequency::k24Mhz:
      ref_clock_khz_ = 24'000;
      break;
    case tgl_registers::Dssm::RefFrequency::k38_4Mhz:
      ref_clock_khz_ = 38'400;
      break;
    default:
      // Unreachable
      ZX_DEBUG_ASSERT(false);
  }

  auto cdclk_pll_enable = tgl_registers::IclCdClkPllEnable::Get().ReadFrom(mmio_space_);
  if (!cdclk_pll_enable.pll_lock()) {
    // CDCLK is disabled. No need to load |state_|.
    enabled_ = false;
    return true;
  }

  enabled_ = true;
  state_.pll_ratio = cdclk_pll_enable.pll_ratio();

  auto cdclk_ctl = tgl_registers::CdClockCtl::Get().ReadFrom(mmio_space_);
  auto divider = cdclk_ctl.icl_cd2x_divider_select();
  switch (divider) {
    case tgl_registers::CdClockCtl::kCd2xDivider1:
      state_.cd2x_divider = 1;
      break;
    case tgl_registers::CdClockCtl::kCd2xDivider2:
      state_.cd2x_divider = 2;
      break;
    default:
      ZX_DEBUG_ASSERT_MSG(false, "Invalid CD2X divider value: 0x%x", divider);
  }

  uint32_t freq_khz = ref_clock_khz_ * state_.pll_ratio / state_.cd2x_divider / 2;
  if (cdclk_ctl.cd_freq_decimal() != tgl_registers::CdClockCtl::FreqDecimal(freq_khz)) {
    zxlogf(ERROR,
           "The CD frequency value (0x%x) doesn't match loaded hardware "
           "state (ref_clock %u KHz, pll ratio %u, cd2x divider %u)",
           cdclk_ctl.cd_freq_decimal(), ref_clock_khz_, state_.pll_ratio, state_.cd2x_divider);
    return false;
  }

  set_current_freq_khz(freq_khz);

  return true;
}

std::optional<CoreDisplayClockTigerLake::State> CoreDisplayClockTigerLake::FreqToState(
    uint32_t freq_khz) const {
  switch (ref_clock_khz_) {
    case 19'200:
    case 38'400:
      switch (freq_khz) {
        case 172'800:
        case 192'000:
        case 307'200:
        case 556'800:
        case 652'800:
          return CoreDisplayClockTigerLake::State{
              .cd2x_divider = 1,
              .pll_ratio = freq_khz * 2 / ref_clock_khz_,
          };
        case 326'400:
          return CoreDisplayClockTigerLake::State{
              .cd2x_divider = 2,
              .pll_ratio = freq_khz * 4 / ref_clock_khz_,
          };
        default:
          // Invalid frequency
          return std::nullopt;
      }
    case 24'000:
      switch (freq_khz) {
        case 180'000:
        case 192'000:
        case 312'000:
        case 552'000:
        case 648'000:
          return CoreDisplayClockTigerLake::State{
              .cd2x_divider = 1,
              .pll_ratio = freq_khz * 2 / ref_clock_khz_,
          };
        case 324'000:
          return CoreDisplayClockTigerLake::State{
              .cd2x_divider = 2,
              .pll_ratio = freq_khz * 4 / ref_clock_khz_,
          };
        default:
          // Invalid frequency
          return std::nullopt;
      }
    default:
      // Unreachable
      ZX_DEBUG_ASSERT(false);
      return std::nullopt;
  }
}

bool CoreDisplayClockTigerLake::CheckFrequency(uint32_t freq_khz) {
  return freq_khz == 0 || FreqToState(freq_khz).has_value();
}

bool CoreDisplayClockTigerLake::SetFrequency(uint32_t freq_khz) {
  if (!CheckFrequency(freq_khz)) {
    zxlogf(ERROR, "Tiger Lake CDCLK SetFrequency: Invalid frequency %u KHz", freq_khz);
    return false;
  }

  // Changing CD Clock Frequency specified on
  // intel-gfx-prm-osrc-tgl-vol12-displayengine_0.pdf p.200
  if (!PreChangeFreq()) {
    return false;
  }
  if (!ChangeFreq(freq_khz)) {
    return false;
  }
  if (!PostChangeFreq(freq_khz)) {
    return false;
  }
  set_current_freq_khz(freq_khz);
  return true;
}

bool CoreDisplayClockTigerLake::PreChangeFreq() {
  bool raise_voltage_result = WriteToGtMailbox(mmio_space_, {
                                                                .addr = 0x80000007,
                                                                .val = 0x3,
                                                                .poll_busy_timeout_us = 150,
                                                                .poll_freq_us = 150,
                                                                .total_timeout_us = 3000,
                                                            });
  if (!raise_voltage_result) {
    zxlogf(ERROR, "Set CDCLK: Failed to raise voltage to max level");
    return false;
  }
  return true;
}

bool CoreDisplayClockTigerLake::PostChangeFreq(uint32_t freq_khz) {
  bool set_voltage_result = WriteToGtMailbox(mmio_space_, {
                                                              .addr = 0x80000007,
                                                              .val = FreqToVoltageLevel(freq_khz),
                                                              .poll_busy_timeout_us = 0,
                                                              .poll_freq_us = 0,
                                                              .total_timeout_us = 0,
                                                          });
  if (!set_voltage_result) {
    zxlogf(ERROR, "Set CDCLK: Failed to set voltage");
    return false;
  }
  return true;
}

bool CoreDisplayClockTigerLake::Enable(uint32_t freq_khz, State state) {
  if (enabled_) {
    // We shouldn't enable the CDCLK twice, unless the target state
    // is exactly the same as current state, in which case it will be a no-op.
    return freq_khz == current_freq_khz() && state.cd2x_divider == state_.cd2x_divider &&
           state.pll_ratio == state_.pll_ratio;
  }

  // Write CDCLK_PLL_ENABLE with the PLL ratio, but not yet enabling it.
  auto cdclk_pll_enable = tgl_registers::IclCdClkPllEnable::Get().ReadFrom(mmio_space_);
  cdclk_pll_enable.set_pll_ratio(state.pll_ratio);
  cdclk_pll_enable.WriteTo(mmio_space_);

  // Set CDCLK_PLL_ENABLE PLL Enable
  cdclk_pll_enable.set_pll_enable(1);
  cdclk_pll_enable.WriteTo(mmio_space_);

  // Poll CDCLK_PLL_ENABLE for PLL lock. Timeout and fail if not locked after
  // 200 us.
  if (!PollUntil([&] { return cdclk_pll_enable.ReadFrom(mmio_space_).pll_lock(); }, zx::usec(1),
                 200)) {
    zxlogf(ERROR, "Tiger Lake CDCLK Enable: Timeout");
    return false;
  }

  // Write CDCLK_CTL with the CD2X Divider selection and CD Frequency Decimal
  // value to match the desired CD clock frequency.
  auto cdclk_ctl = tgl_registers::CdClockCtl::Get().ReadFrom(mmio_space_);
  switch (state.cd2x_divider) {
    case 1:
      cdclk_ctl.set_icl_cd2x_divider_select(tgl_registers::CdClockCtl::kCd2xDivider1);
      break;
    case 2:
      cdclk_ctl.set_icl_cd2x_divider_select(tgl_registers::CdClockCtl::kCd2xDivider2);
      break;
    default:
      ZX_DEBUG_ASSERT(false);
      return false;
  }

  cdclk_ctl.set_cd_freq_decimal(tgl_registers::CdClockCtl::FreqDecimal(freq_khz));
  cdclk_ctl.WriteTo(mmio_space_);

  state_ = state;
  enabled_ = true;
  return true;
}

bool CoreDisplayClockTigerLake::Disable() {
  if (!enabled_) {
    // No-op if CDCLK is always disabled.
    return true;
  }

  // Clear CDCLK_PLL_ENABLE PLL Enable
  auto cdclk_pll_enable = tgl_registers::IclCdClkPllEnable::Get().ReadFrom(mmio_space_);
  cdclk_pll_enable.set_pll_enable(0);
  cdclk_pll_enable.WriteTo(mmio_space_);

  // Poll CDCLK_PLL_ENABLE for PLL unlocked. Timeout and fail if not unlocked
  // after 200 us.
  if (!PollUntil([&] { return !cdclk_pll_enable.ReadFrom(mmio_space_).pll_lock(); }, zx::usec(1),
                 200)) {
    zxlogf(ERROR, "Tiger Lake CDCLK Disable: Timeout");
    return false;
  }
  enabled_ = false;
  return true;
}

bool CoreDisplayClockTigerLake::ChangeFreq(uint32_t freq_khz) {
  if (freq_khz == 0) {
    return Disable();
  }

  auto new_state_maybe = FreqToState(freq_khz);
  if (!new_state_maybe.has_value()) {
    ZX_DEBUG_ASSERT(false);
    return false;
  }

  auto new_state = new_state_maybe.value();
  if (enabled_ && new_state.pll_ratio == state_.pll_ratio) {
    if (new_state.cd2x_divider != state_.cd2x_divider) {
      // Changing only the CD2X divider:
      // Write CDCLK_CTL with the CD2X Divider selection, and CD Frequency
      // Decimal value to match the desired CD clock frequency.
      auto cdclk_ctl = tgl_registers::CdClockCtl::Get().ReadFrom(mmio_space_);
      switch (new_state.cd2x_divider) {
        case 1:
          cdclk_ctl.set_icl_cd2x_divider_select(tgl_registers::CdClockCtl::kCd2xDivider1);
          break;
        case 2:
          cdclk_ctl.set_icl_cd2x_divider_select(tgl_registers::CdClockCtl::kCd2xDivider2);
          break;
        default:
          ZX_DEBUG_ASSERT(false);
          return false;
      }
      cdclk_ctl.set_cd_freq_decimal(tgl_registers::CdClockCtl::FreqDecimal(freq_khz));
      cdclk_ctl.WriteTo(mmio_space_);
    }
    // Otherwise the state doesn't change; it's a no-op.
  } else {
    // If changing the CDCLK PLL frequency, we need to first disable CDCLK PLL,
    // then enable CDCLK PLL using the new PLL ratio.
    if (!Disable()) {
      zxlogf(ERROR, "Cannot disable CDCLK");
      return false;
    }
    if (!Enable(freq_khz, new_state)) {
      zxlogf(ERROR, "Cannot enable CDCLK");
      return false;
    }
  }

  set_current_freq_khz(freq_khz);
  return true;
}

// static
uint32_t CoreDisplayClockTigerLake::FreqToVoltageLevel(uint32_t freq_khz) {
  if (freq_khz > 556'800) {
    return 0x3;
  }
  if (freq_khz > 326'400) {
    return 0x2;
  }
  if (freq_khz > 312'000) {
    return 0x1;
  }
  return 0x0;
}

}  // namespace i915_tgl
