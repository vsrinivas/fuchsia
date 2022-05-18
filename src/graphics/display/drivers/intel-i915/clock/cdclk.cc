// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915/clock/cdclk.h"

#include <lib/ddk/debug.h>
#include <zircon/assert.h>

#include <cstdint>

#include "src/graphics/display/drivers/intel-i915/macros.h"
#include "src/graphics/display/drivers/intel-i915/registers-dpll.h"
#include "src/graphics/display/drivers/intel-i915/registers.h"

namespace i915 {

namespace {

struct GtDriverMailboxOp {
  uint32_t addr;
  uint64_t val;
  uint32_t poll_freq_us = 0;
  uint32_t timeout_us = 0;
};

bool WriteToGtMailbox(fdf::MmioBuffer* mmio_space, GtDriverMailboxOp op) {
  constexpr uint32_t kGtDriverMailboxInterface = 0x138124;
  constexpr uint32_t kGtDriverMailboxData0 = 0x138128;
  constexpr uint32_t kGtDriverMailboxData1 = 0x13812c;

  for (uint32_t total_wait_us = 0;;) {
    mmio_space->Write32(kGtDriverMailboxData0, static_cast<uint32_t>(op.val & 0xffffffff));
    mmio_space->Write32(kGtDriverMailboxData1, static_cast<uint32_t>(op.val >> 32));
    mmio_space->Write32(kGtDriverMailboxInterface, op.addr);

    if (op.timeout_us == 0 || op.poll_freq_us == 0) {
      return true;
    }

    if (!WAIT_ON_US(mmio_space->Read32(kGtDriverMailboxInterface) & 0x80000000,
                    static_cast<int32_t>(op.poll_freq_us))) {
      zxlogf(ERROR, "GT Driver Mailbox driver busy");
      return false;
    }
    if (mmio_space->Read32(kGtDriverMailboxData0) & 0x1) {
      break;
    }
    total_wait_us += op.poll_freq_us;
    if (total_wait_us >= op.timeout_us) {
      zxlogf(ERROR, "GT Driver Mailbox: Write timeout");
      return false;
    }
  }
  return true;
}

uint32_t SklCdClockFreqToVoltageLevel(uint32_t freq_khz) {
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

}  // namespace

SklCoreDisplayClock::SklCoreDisplayClock(fdf::MmioBuffer* mmio_space) : mmio_space_(mmio_space) {
  bool current_freq_is_valid = LoadState();
  ZX_DEBUG_ASSERT(current_freq_is_valid);
}

bool SklCoreDisplayClock::LoadState() {
  auto dpll_enable = registers::DpllEnable::Get(registers::DPLL_0).ReadFrom(mmio_space_);
  if (!dpll_enable.enable_dpll()) {
    zxlogf(ERROR, "SKL CDCLK LoadState: DPLL0 is disabled");
    return false;
  }

  auto dpll_ctrl1 = registers::DpllControl1::Get().ReadFrom(mmio_space_);
  auto cdclk_ctl = registers::CdClockCtl::Get().ReadFrom(mmio_space_);
  auto cd_freq_select = cdclk_ctl.cd_freq_select();

  auto link_rate = dpll_ctrl1.GetLinkRate(registers::DPLL_0);
  bool is_vco_8640 = (link_rate == registers::DpllControl1::LinkRate::k1080Mhz) ||
                     (link_rate == registers::DpllControl1::LinkRate::k2160Mhz);

  switch (cd_freq_select) {
    case registers::CdClockCtl::kFreqSelect3XX:
      set_current_freq_khz(is_vco_8640 ? 308'570 : 337'500);
      break;
    case registers::CdClockCtl::kFreqSelect4XX:
      set_current_freq_khz(is_vco_8640 ? 432'000 : 450'000);
      break;
    case registers::CdClockCtl::kFreqSelect540:
      set_current_freq_khz(540'000);
      break;
    case registers::CdClockCtl::kFreqSelect6XX:
      set_current_freq_khz(is_vco_8640 ? 617'140 : 675'000);
      break;
    default:
      zxlogf(ERROR, "Invalid CD Clock frequency");
      return false;
  }

  return true;
}

bool SklCoreDisplayClock::PreChangeFreq() {
  bool raise_voltage_result = WriteToGtMailbox(mmio_space_, {
                                                                .addr = 0x80000007,
                                                                .val = 0x3,
                                                                .poll_freq_us = 150,
                                                                .timeout_us = 3000,
                                                            });
  if (!raise_voltage_result) {
    zxlogf(ERROR, "Set CDCLK: Failed to raise voltage to max level");
    return false;
  }
  return true;
}

bool SklCoreDisplayClock::PostChangeFreq(uint32_t freq_khz) {
  bool set_voltage_result =
      WriteToGtMailbox(mmio_space_, {
                                        .addr = 0x80000007,
                                        .val = SklCdClockFreqToVoltageLevel(freq_khz),
                                        .poll_freq_us = 0,
                                        .timeout_us = 0,
                                    });
  if (!set_voltage_result) {
    zxlogf(ERROR, "Set CDCLK: Failed to set voltage");
    return false;
  }
  return true;
}

bool SklCoreDisplayClock::CheckFrequency(uint32_t freq_khz) {
  auto dpll_enable = registers::DpllEnable::Get(registers::DPLL_0).ReadFrom(mmio_space_);
  if (!dpll_enable.enable_dpll()) {
    zxlogf(ERROR, "SKL CDCLK CheckFrequency: DPLL0 is disabled");
    return false;
  }

  auto dpll_ctrl1 = registers::DpllControl1::Get().ReadFrom(mmio_space_);
  auto link_rate = dpll_ctrl1.GetLinkRate(registers::DPLL_0);
  bool is_vco_8640 = link_rate == registers::DpllControl1::LinkRate::k1080Mhz ||
                     link_rate == registers::DpllControl1::LinkRate::k2160Mhz;
  if (is_vco_8640) {
    // VCO 8640
    return freq_khz == 308'570 || freq_khz == 432'000 || freq_khz == 540'000 || freq_khz == 617'140;
  }
  // VCO 8100
  return freq_khz == 337'500 || freq_khz == 450'000 || freq_khz == 540'000 || freq_khz == 675'000;
}

bool SklCoreDisplayClock::ChangeFreq(uint32_t freq_khz) {
  // Set the cd_clk frequency to |freq_khz|.
  auto cd_clk = registers::CdClockCtl::Get().ReadFrom(mmio_space_);
  switch (freq_khz) {
    case 308'570:
      cd_clk.set_cd_freq_select(registers::CdClockCtl::kFreqSelect3XX);
      cd_clk.set_cd_freq_decimal(registers::CdClockCtl::kFreqDecimal30857);
      break;
    case 337'500:
      cd_clk.set_cd_freq_select(registers::CdClockCtl::kFreqSelect3XX);
      cd_clk.set_cd_freq_decimal(registers::CdClockCtl::kFreqDecimal3375);
      break;
    case 432'000:
      cd_clk.set_cd_freq_select(registers::CdClockCtl::kFreqSelect4XX);
      cd_clk.set_cd_freq_decimal(registers::CdClockCtl::kFreqDecimal432);
      break;
    case 450'000:
      cd_clk.set_cd_freq_select(registers::CdClockCtl::kFreqSelect4XX);
      cd_clk.set_cd_freq_decimal(registers::CdClockCtl::kFreqDecimal450);
      break;
    case 540'000:
      cd_clk.set_cd_freq_select(registers::CdClockCtl::kFreqSelect540);
      cd_clk.set_cd_freq_decimal(registers::CdClockCtl::kFreqDecimal540);
      break;
    case 617'140:
      cd_clk.set_cd_freq_select(registers::CdClockCtl::kFreqSelect6XX);
      cd_clk.set_cd_freq_decimal(registers::CdClockCtl::kFreqDecimal61714);
      break;
    case 675'000:
      cd_clk.set_cd_freq_select(registers::CdClockCtl::kFreqSelect6XX);
      cd_clk.set_cd_freq_decimal(registers::CdClockCtl::kFreqDecimal675);
      break;
    default:
      // Unreachable
      ZX_DEBUG_ASSERT(false);
      return false;
  }
  cd_clk.WriteTo(mmio_space_);
  return true;
}

bool SklCoreDisplayClock::SetFrequency(uint32_t freq_khz) {
  if (!CheckFrequency(freq_khz)) {
    zxlogf(ERROR, "SKL CDCLK ChangeFreq: Invalid frequency %u KHz", freq_khz);
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

}  // namespace i915
