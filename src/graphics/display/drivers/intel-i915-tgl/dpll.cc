// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/dpll.h"

#include <lib/zx/time.h>
#include <zircon/assert.h>

#include <optional>
#include <variant>

#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"
#include "src/graphics/display/drivers/intel-i915-tgl/poll-until.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-ddi.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-dpll.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-typec.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers.h"

namespace i915_tgl {

namespace {

std::string GetDpllName(tgl_registers::Dpll dpll) {
  switch (dpll) {
    case tgl_registers::DPLL_0:
      return "DPLL 0";
    case tgl_registers::DPLL_1:
      return "DPLL 1";
    case tgl_registers::DPLL_2:
      return "DPLL 2";
    case tgl_registers::DPLL_3:
      return "DPLL 3";
    case tgl_registers::DPLL_TC_1:
      return "DPLL TC 1";
    case tgl_registers::DPLL_TC_2:
      return "DPLL TC 2";
    case tgl_registers::DPLL_TC_3:
      return "DPLL TC 3";
    case tgl_registers::DPLL_TC_4:
      return "DPLL TC 4";
    case tgl_registers::DPLL_TC_5:
      return "DPLL TC 5";
    case tgl_registers::DPLL_TC_6:
      return "DPLL TC 6";
    default:
      return "DPLL Invalid";
  }
}

bool CompareDpllStates(const DpllState& a, const DpllState& b) {
  if (std::holds_alternative<DpDpllState>(a)) {
    if (!std::holds_alternative<DpDpllState>(b)) {
      return false;
    }

    const auto& dp_a = std::get<DpDpllState>(a);
    const auto& dp_b = std::get<DpDpllState>(b);
    return dp_a.dp_bit_rate_mhz == dp_b.dp_bit_rate_mhz;
  }

  if (std::holds_alternative<HdmiDpllState>(a)) {
    if (!std::holds_alternative<HdmiDpllState>(b)) {
      return false;
    }

    const auto& hdmi_a = std::get<HdmiDpllState>(a);
    const auto& hdmi_b = std::get<HdmiDpllState>(b);

    return hdmi_a.dco_int == hdmi_b.dco_int && hdmi_a.dco_frac == hdmi_b.dco_frac &&
           hdmi_a.q == hdmi_b.q && hdmi_a.q_mode == hdmi_b.q_mode && hdmi_a.k == hdmi_b.k &&
           hdmi_a.p == hdmi_b.p && hdmi_a.cf == hdmi_b.cf;
  }

  ZX_DEBUG_ASSERT_MSG(false, "Comparing unsupported DpllState");
  return false;
}

std::optional<tgl_registers::DpllControl1::LinkRate> DpBitRateMhzToSkylakeLinkRate(
    uint32_t dp_bit_rate_mhz) {
  switch (dp_bit_rate_mhz) {
    case 5400:
      return tgl_registers::DpllControl1::LinkRate::k2700Mhz;
    case 2700:
      return tgl_registers::DpllControl1::LinkRate::k1350Mhz;
    case 1620:
      return tgl_registers::DpllControl1::LinkRate::k810Mhz;
    case 3240:
      return tgl_registers::DpllControl1::LinkRate::k1620Mhz;
    case 2160:
      return tgl_registers::DpllControl1::LinkRate::k1080Mhz;
    case 4320:
      return tgl_registers::DpllControl1::LinkRate::k2160Mhz;
    default:
      return std::nullopt;
  }
}

std::optional<uint32_t> SkylakeLinkRateToDpBitRateMhz(
    tgl_registers::DpllControl1::LinkRate link_rate) {
  switch (link_rate) {
    case tgl_registers::DpllControl1::LinkRate::k2700Mhz:
      return 5400;
    case tgl_registers::DpllControl1::LinkRate::k1350Mhz:
      return 2700;
    case tgl_registers::DpllControl1::LinkRate::k810Mhz:
      return 1620;
    case tgl_registers::DpllControl1::LinkRate::k1620Mhz:
      return 3240;
    case tgl_registers::DpllControl1::LinkRate::k1080Mhz:
      return 2160;
    case tgl_registers::DpllControl1::LinkRate::k2160Mhz:
      return 4320;
    default:
      return std::nullopt;
  }
}

}  // namespace

DisplayPll::DisplayPll(tgl_registers::Dpll dpll) : dpll_(dpll), name_(GetDpllName(dpll)) {}

DisplayPll* DisplayPllManager::Map(tgl_registers::Ddi ddi, bool is_edp, const DpllState& state) {
  if (ddi_to_dpll_.find(ddi) != ddi_to_dpll_.end()) {
    Unmap(ddi);
  }

  DisplayPll* best_dpll = FindBestDpll(ddi, is_edp, state);
  if (!best_dpll) {
    zxlogf(ERROR, "Cannot find an available DPLL for DDI %d", ddi);
    return nullptr;
  }

  if (ref_count_[best_dpll] > 0 || best_dpll->Enable(state)) {
    if (!MapImpl(ddi, best_dpll->dpll())) {
      zxlogf(ERROR, "Failed to map DDI %d to DPLL (%s)", ddi, best_dpll->name().c_str());
      return nullptr;
    }
    ref_count_[best_dpll]++;
    ddi_to_dpll_[ddi] = best_dpll;
    return best_dpll;
  }
  return nullptr;
}

bool DisplayPllManager::Unmap(tgl_registers::Ddi ddi) {
  if (ddi_to_dpll_.find(ddi) == ddi_to_dpll_.end()) {
    return true;
  }

  DisplayPll* dpll = ddi_to_dpll_[ddi];
  if (!UnmapImpl(ddi)) {
    zxlogf(ERROR, "Failed to unmap DPLL (%s) for DDI %d", dpll->name().c_str(), ddi);
    return false;
  }

  ZX_DEBUG_ASSERT(ref_count_[dpll] > 0);
  if (--ref_count_[dpll] == 0) {
    ddi_to_dpll_.erase(ddi);
    return dpll->Disable();
  }
  return true;
}

bool DisplayPllManager::PllNeedsReset(tgl_registers::Ddi ddi, const DpllState& new_state) {
  if (ddi_to_dpll_.find(ddi) == ddi_to_dpll_.end()) {
    return true;
  }
  return !CompareDpllStates(ddi_to_dpll_[ddi]->state(), new_state);
}

DpllSkylake::DpllSkylake(fdf::MmioBuffer* mmio_space, tgl_registers::Dpll dpll)
    : DisplayPll(dpll), mmio_space_(mmio_space) {}

bool DpllSkylake::Enable(const DpllState& state) {
  if (enabled_) {
    zxlogf(ERROR, "DPLL (%s) Enable(): Already enabled!", name().c_str());
    return false;
  }

  if (std::holds_alternative<HdmiDpllState>(state)) {
    enabled_ = EnableHdmi(std::get<HdmiDpllState>(state));
  } else {
    enabled_ = EnableDp(std::get<DpDpllState>(state));
  }

  if (enabled_) {
    set_state(state);
  }
  return enabled_;
}

bool DpllSkylake::EnableDp(const DpDpllState& dp_state) {
  // Configure this DPLL to produce a suitable clock signal.
  auto dpll_ctrl1 = tgl_registers::DpllControl1::Get().ReadFrom(mmio_space_);
  dpll_ctrl1.dpll_hdmi_mode(dpll()).set(0);
  dpll_ctrl1.dpll_ssc_enable(dpll()).set(0);
  auto dp_rate = DpBitRateMhzToSkylakeLinkRate(dp_state.dp_bit_rate_mhz);
  if (!dp_rate.has_value()) {
    zxlogf(ERROR, "Invalid DP bit rate: %u MHz", dp_state.dp_bit_rate_mhz);
    return false;
  }
  dpll_ctrl1.SetLinkRate(dpll(), *dp_rate);
  dpll_ctrl1.dpll_override(dpll()).set(1);
  dpll_ctrl1.WriteTo(mmio_space_);
  dpll_ctrl1.ReadFrom(mmio_space_);  // Posting read

  // Enable this DPLL and wait for it to lock
  auto dpll_enable = tgl_registers::DpllEnable::GetForSkylakeDpll(dpll()).ReadFrom(mmio_space_);
  dpll_enable.set_enable_dpll(1);
  dpll_enable.WriteTo(mmio_space_);
  if (!PollUntil(
          [&] {
            return tgl_registers::DpllStatus::Get().ReadFrom(mmio_space_).dpll_lock(dpll()).get();
          },
          zx::msec(1), 5)) {
    zxlogf(ERROR, "DPLL failed to lock");
    return false;
  }

  return true;
}

bool DpllSkylake::EnableHdmi(const HdmiDpllState& hdmi_state) {
  // Set the DPLL control settings
  auto dpll_ctrl1 = tgl_registers::DpllControl1::Get().ReadFrom(mmio_space_);
  dpll_ctrl1.dpll_hdmi_mode(dpll()).set(1);
  dpll_ctrl1.dpll_override(dpll()).set(1);
  dpll_ctrl1.dpll_ssc_enable(dpll()).set(0);
  dpll_ctrl1.WriteTo(mmio_space_);
  dpll_ctrl1.ReadFrom(mmio_space_);  // Posting read

  // Set the DCO frequency
  auto dpll_cfg1 = tgl_registers::DpllConfig1::Get(dpll()).FromValue(0);
  dpll_cfg1.set_frequency_enable(1);
  dpll_cfg1.set_dco_integer(hdmi_state.dco_int);
  dpll_cfg1.set_dco_fraction(hdmi_state.dco_frac);
  dpll_cfg1.WriteTo(mmio_space_);
  dpll_cfg1.ReadFrom(mmio_space_);  // Posting read

  // Set the divisors and central frequency
  auto dpll_cfg2 = tgl_registers::DpllConfig2::Get(dpll()).FromValue(0);
  dpll_cfg2.set_qdiv_ratio(hdmi_state.q);
  dpll_cfg2.set_qdiv_mode(hdmi_state.q_mode);
  dpll_cfg2.set_kdiv_ratio(hdmi_state.k);
  dpll_cfg2.set_pdiv_ratio(hdmi_state.p);
  dpll_cfg2.set_central_freq(hdmi_state.cf);
  dpll_cfg2.WriteTo(mmio_space_);
  dpll_cfg2.ReadFrom(mmio_space_);  // Posting read

  // Enable and wait for the DPLL
  auto dpll_enable = tgl_registers::DpllEnable::GetForSkylakeDpll(dpll()).ReadFrom(mmio_space_);
  dpll_enable.set_enable_dpll(1);
  dpll_enable.WriteTo(mmio_space_);
  if (!PollUntil(
          [&] {
            return tgl_registers::DpllStatus ::Get().ReadFrom(mmio_space_).dpll_lock(dpll()).get();
          },
          zx::msec(1), 5)) {
    zxlogf(ERROR, "hdmi: DPLL failed to lock");
    return false;
  }

  return true;
}

bool DpllSkylake::Disable() {
  if (!enabled_) {
    zxlogf(INFO, "Dpll %s Disable(): Already disabled", name().c_str());
    return true;
  }

  // We don't want to disable DPLL0, since that drives cdclk.
  if (dpll() != tgl_registers::DPLL_0) {
    auto dpll_enable = tgl_registers::DpllEnable::GetForSkylakeDpll(dpll()).ReadFrom(mmio_space_);
    dpll_enable.set_enable_dpll(0);
    dpll_enable.WriteTo(mmio_space_);
  }
  enabled_ = false;

  return true;
}

DpllManagerSkylake::DpllManagerSkylake(fdf::MmioBuffer* mmio_space) : mmio_space_(mmio_space) {
  for (const auto dpll : tgl_registers::Dplls<tgl_registers::Platform::kSkylake>()) {
    plls_[dpll] = std::unique_ptr<DpllSkylake>(new DpllSkylake(mmio_space, dpll));
    ref_count_[plls_[dpll].get()] = 0;
  }
}

bool DpllManagerSkylake::MapImpl(tgl_registers::Ddi ddi, tgl_registers::Dpll dpll) {
  // Direct the DPLL to the DDI
  auto dpll_ctrl2 = tgl_registers::DpllControl2::Get().ReadFrom(mmio_space_);
  dpll_ctrl2.ddi_select_override(ddi).set(1);
  dpll_ctrl2.ddi_clock_off(ddi).set(0);
  dpll_ctrl2.ddi_clock_select(ddi).set(dpll);
  dpll_ctrl2.WriteTo(mmio_space_);

  return true;
}

bool DpllManagerSkylake::UnmapImpl(tgl_registers::Ddi ddi) {
  auto dpll_ctrl2 = tgl_registers::DpllControl2::Get().ReadFrom(mmio_space_);
  dpll_ctrl2.ddi_clock_off(ddi).set(1);
  dpll_ctrl2.WriteTo(mmio_space_);

  return true;
}

std::optional<DpllState> DpllManagerSkylake::LoadState(tgl_registers::Ddi ddi) {
  auto dpll_ctrl2 = tgl_registers::DpllControl2::Get().ReadFrom(mmio_space_);
  if (dpll_ctrl2.ddi_clock_off(ddi).get()) {
    return std::nullopt;
  }

  auto dpll = static_cast<tgl_registers::Dpll>(dpll_ctrl2.ddi_clock_select(ddi).get());
  auto dpll_enable = tgl_registers::DpllEnable::GetForSkylakeDpll(dpll).ReadFrom(mmio_space_);
  if (!dpll_enable.enable_dpll()) {
    return std::nullopt;
  }

  // Remove stale mappings first.
  if (ddi_to_dpll_.find(ddi) != ddi_to_dpll_.end()) {
    ZX_DEBUG_ASSERT(ref_count_.find(ddi_to_dpll_[ddi]) != ref_count_.end() &&
                    ref_count_.at(ddi_to_dpll_[ddi]) > 0);
    --ref_count_[ddi_to_dpll_[ddi]];
    ddi_to_dpll_.erase(ddi);
  }

  ddi_to_dpll_[ddi] = plls_[dpll].get();
  ++ref_count_[ddi_to_dpll_[ddi]];

  DpllState new_state;
  auto dpll_ctrl1 = tgl_registers::DpllControl1::Get().ReadFrom(mmio_space_);
  bool is_hdmi = dpll_ctrl1.dpll_hdmi_mode(dpll).get();
  if (is_hdmi) {
    auto dpll_cfg1 = tgl_registers::DpllConfig1::Get(dpll).ReadFrom(mmio_space_);
    auto dpll_cfg2 = tgl_registers::DpllConfig2::Get(dpll).ReadFrom(mmio_space_);

    new_state = HdmiDpllState{
        .dco_int = static_cast<uint16_t>(dpll_cfg1.dco_integer()),
        .dco_frac = static_cast<uint16_t>(dpll_cfg1.dco_fraction()),
        .q = static_cast<uint8_t>(dpll_cfg2.qdiv_ratio()),
        .q_mode = static_cast<uint8_t>(dpll_cfg2.qdiv_mode()),
        .k = static_cast<uint8_t>(dpll_cfg2.kdiv_ratio()),
        .p = static_cast<uint8_t>(dpll_cfg2.pdiv_ratio()),
        .cf = static_cast<uint8_t>(dpll_cfg2.central_freq()),
    };
  } else {
    auto dp_bit_rate_mhz = SkylakeLinkRateToDpBitRateMhz(dpll_ctrl1.GetLinkRate(dpll));
    if (!dp_bit_rate_mhz.has_value()) {
      zxlogf(ERROR, "Invalid DPLL link rate from DPLL %d", dpll);
      return std::nullopt;
    }

    new_state = DpDpllState{
        .dp_bit_rate_mhz = *dp_bit_rate_mhz,
    };
  }

  return std::make_optional(new_state);
}

DisplayPll* DpllManagerSkylake::FindBestDpll(tgl_registers::Ddi ddi, bool is_edp,
                                             const DpllState& state) {
  DisplayPll* res = nullptr;
  if (is_edp) {
    ZX_DEBUG_ASSERT(std::holds_alternative<DpDpllState>(state));

    DisplayPll* dpll_0 = plls_[tgl_registers::DPLL_0].get();
    if (ref_count_[dpll_0] == 0 || CompareDpllStates(dpll_0->state(), state)) {
      res = dpll_0;
    }
  } else {
    DisplayPll* const kCandidates[] = {
        plls_[tgl_registers::Dpll::DPLL_1].get(),
        plls_[tgl_registers::Dpll::DPLL_3].get(),
        plls_[tgl_registers::Dpll::DPLL_2].get(),
    };
    for (DisplayPll* candidate : kCandidates) {
      if (ref_count_[candidate] == 0 || CompareDpllStates(candidate->state(), state)) {
        res = candidate;
        break;
      }
    }
  }

  if (res) {
    zxlogf(DEBUG, "Will select DPLL %s", res->name().c_str());
  } else {
    zxlogf(WARNING, "Failed to allocate DPLL");
  }

  return res;
}

namespace {

tgl_registers::Dpll TypeCDdiToDekelPll(tgl_registers::Ddi type_c_ddi) {
  switch (type_c_ddi) {
    case tgl_registers::Ddi::DDI_TC_1:
      return tgl_registers::Dpll::DPLL_TC_1;
    case tgl_registers::Ddi::DDI_TC_2:
      return tgl_registers::Dpll::DPLL_TC_2;
    case tgl_registers::Ddi::DDI_TC_3:
      return tgl_registers::Dpll::DPLL_TC_3;
    case tgl_registers::Ddi::DDI_TC_4:
      return tgl_registers::Dpll::DPLL_TC_4;
    case tgl_registers::Ddi::DDI_TC_5:
      return tgl_registers::Dpll::DPLL_TC_5;
    case tgl_registers::Ddi::DDI_TC_6:
      return tgl_registers::Dpll::DPLL_TC_6;
    default:
      ZX_ASSERT_MSG(false, "Not a Type-C DDI");
  }
}

tgl_registers::Ddi DekelPllToTypeCDdi(tgl_registers::Dpll dekel_pll) {
  switch (dekel_pll) {
    case tgl_registers::Dpll::DPLL_TC_1:
      return tgl_registers::Ddi::DDI_TC_1;
    case tgl_registers::Dpll::DPLL_TC_2:
      return tgl_registers::Ddi::DDI_TC_2;
    case tgl_registers::Dpll::DPLL_TC_3:
      return tgl_registers::Ddi::DDI_TC_3;
    case tgl_registers::Dpll::DPLL_TC_4:
      return tgl_registers::Ddi::DDI_TC_4;
    case tgl_registers::Dpll::DPLL_TC_5:
      return tgl_registers::Ddi::DDI_TC_5;
    case tgl_registers::Dpll::DPLL_TC_6:
      return tgl_registers::Ddi::DDI_TC_6;
    default:
      ZX_ASSERT_MSG(false, "Not a Dekel PLL");
  }
}

}  // namespace

DekelPllTigerLake::DekelPllTigerLake(fdf::MmioBuffer* mmio_space, tgl_registers::Dpll dpll)
    : DisplayPll(dpll), mmio_space_(mmio_space) {}

tgl_registers::Ddi DekelPllTigerLake::ddi_id() const {
  ZX_DEBUG_ASSERT(dpll() >= tgl_registers::DPLL_TC_1);
  ZX_DEBUG_ASSERT(dpll() <= tgl_registers::DPLL_TC_6);
  return DekelPllToTypeCDdi(dpll());
}

bool DekelPllTigerLake::Enable(const DpllState& state) {
  if (enabled_) {
    zxlogf(WARNING, "Dekel PLL %s Enable: Already enabled", name().c_str());
    return true;
  }

  if (std::holds_alternative<HdmiDpllState>(state)) {
    enabled_ = EnableHdmi(std::get<HdmiDpllState>(state));
  } else {
    enabled_ = EnableDp(std::get<DpDpllState>(state));
  }

  if (enabled_) {
    set_state(state);
  }
  return enabled_;
}

bool DekelPllTigerLake::Disable() {
  if (!enabled_) {
    zxlogf(WARNING, "Dekel PLL %s Disable: Already disabled", name().c_str());
    return true;
  }

  // Follow the "DKL PLL Disable Sequence" to disable the PLL.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev2.0, Pages 188-189
  //             "DKL PLL Disable Sequence"

  // Step 1. Configure DPCLKA_CFGCR0 to turn off the clock for the port.
  auto ddi_clock_config_control =
      tgl_registers::DdiClockConfigControlRegister0::Get().ReadFrom(mmio_space_);
  ddi_clock_config_control.turn_off_clock_for_ddi(ddi_id(), /*turn_off=*/true)
      .WriteTo(mmio_space_)
      .ReadFrom(mmio_space_);  // Posting read

  // Step 2. If the frequency will result in a change to the voltage requirement,
  // follow the "Display Voltage Frequency Switching - Sequence Before Frequency
  // Change".
  //
  // TODO(fxbug.dev/98533): Currently it is okay to ignore this, unless we need
  // to support 5K+ display where we need to change display voltage and Core
  // Display Clock.

  // 3. Disable PLL through MGPLL_ENABLE.
  auto pll_enable = tgl_registers::DpllEnable::GetForTigerLakeDpll(dpll()).ReadFrom(mmio_space_);
  pll_enable.ReadFrom(mmio_space_).set_enable_dpll(false).WriteTo(mmio_space_);

  // Step 4. Wait for PLL not locked status in MGPLL_ENABLE.
  // Should complete within 50us.
  if (!PollUntil([&] { return pll_enable.ReadFrom(mmio_space_).pll_is_locked() == 0; }, zx::usec(1),
                 50)) {
    zxlogf(ERROR, "Dekel PLL %s: Cannot disable PLL", name().c_str());
  }

  // Step 5. If the frequency will result in a change to the voltage
  // requirement, follow the "Display Voltage Frequency Switching - Sequence
  // After Frequency Change".
  //
  // TODO(fxbug.dev/98533): Currently it is okay to ignore this, unless we need
  // to support 5K+ display where we need to change display voltage and Core
  // Display Clock.

  // 6. Disable PLL power in MGPLL_ENABLE.
  pll_enable.set_power_enable_request_tiger_lake(false);
  pll_enable.WriteTo(mmio_space_);

  // 7. Wait for PLL power state disabled in MGPLL_ENABLE.
  // - Should complete immediately.
  if (!PollUntil(
          [&] { return pll_enable.ReadFrom(mmio_space_).power_is_enabled_tiger_lake() == 0; },
          zx::usec(1), 10)) {
    zxlogf(ERROR, "Dekel PLL %s: Cannot disable PLL power", name().c_str());
  }
  enabled_ = false;

  return true;
}

bool DekelPllTigerLake::EnableHdmi(const HdmiDpllState& state) {
  // TODO(fxbug.dev/109368): Support HDMI on Type-C.
  zxlogf(ERROR, "Dekel PLL %s: EnableHdmi: Not implemented", name().c_str());
  return false;
}

bool DekelPllTigerLake::EnableDp(const DpDpllState& state) {
  zxlogf(TRACE, "Dekel PLL %s: Enable for DisplayPort", name().c_str());

  // This method contains the procedure to enable DisplayPort Mode Dekel PLL.
  // Reference:
  // Tiger Lake: Section "DKL PLL Enable Sequence",
  //             IHD-OS-TGL-Vol 12-1.22-Rev 2.0, Pages 177-178

  // Step 1. Enable PLL power in `MGPLL_ENABLE` register.
  auto pll_enable = tgl_registers::DpllEnable::GetForTigerLakeDpll(dpll()).ReadFrom(mmio_space_);
  pll_enable.set_power_enable_request_tiger_lake(true).WriteTo(mmio_space_);

  // Step 2. Wait for PLL power state enabled in `MGPLL_ENABLE`. Should complete
  // within 10us.
  if (!PollUntil([&] { return pll_enable.ReadFrom(mmio_space_).power_is_enabled_tiger_lake(); },
                 zx::usec(1), 10)) {
    zxlogf(ERROR, "Dekel PLL %s: Cannot enable PLL power", name().c_str());
    return false;
  }

  // Step 3-4. Program PLL registers as given in tables. Read back PHY PLL
  // register after writing to ensure writes completed.
  //
  // Step 3.1. Program rate independent registers for Native and Alt DP mode.
  //
  // Register value table:
  // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev 2.0, Pages 190-191

  // Program DKL_PLL_DIV0.
  auto divisor0 = tgl_registers::DekelPllDivisor0::GetForDdi(ddi_id()).ReadFrom(mmio_space_);
  divisor0.set_reg_value(0x70272269).WriteTo(mmio_space_).ReadFrom(mmio_space_);  // Posting read

  // Program DKL_PLL_DIV1.
  auto divisor1 = tgl_registers::DekelPllDivisor1::GetForDdi(ddi_id()).ReadFrom(mmio_space_);
  divisor1.set_reg_value(0x0CDCC527).WriteTo(mmio_space_).ReadFrom(mmio_space_);  // Posting read

  // Program DKL_PLL_LF.
  auto lf = tgl_registers::DekelPllLf::GetForDdi(ddi_id()).ReadFrom(mmio_space_);
  lf.set_reg_value(0x00401300).WriteTo(mmio_space_).ReadFrom(mmio_space_);  // Posting read

  // Program DKL_PLL_FRAC_LOCK.
  auto frac_lock = tgl_registers::DekelPllFractionalLock::GetForDdi(ddi_id()).ReadFrom(mmio_space_);
  frac_lock.set_reg_value(0x8044B56A).WriteTo(mmio_space_).ReadFrom(mmio_space_);  // Posting read

  // Program DKL_SSC.
  auto ssc_config = tgl_registers::DekelPllSsc::GetForDdi(ddi_id()).ReadFrom(mmio_space_);
  ssc_config.set_reg_value(0x401322FF).WriteTo(mmio_space_).ReadFrom(mmio_space_);  // Posting read

  // Program DKL_CMN_DIG_PLL_MISC.
  auto common_config_digital_pll_misc =
      tgl_registers::DekelCommonConfigDigitalPllMisc::GetForDdi(ddi_id()).ReadFrom(mmio_space_);
  common_config_digital_pll_misc.set_reg_value(0x00000000)
      .WriteTo(mmio_space_)
      .ReadFrom(mmio_space_);  // Posting read

  // Program DKL_REFCLKIN_CTL.
  auto reference_clock_input_control =
      tgl_registers::DekelPllReferenceClockInputControl::GetForDdi(ddi_id()).ReadFrom(mmio_space_);
  reference_clock_input_control.set_reg_value(0x00000101)
      .WriteTo(mmio_space_)
      .ReadFrom(mmio_space_);  // Posting read

  // Program DKL_CMN_ANA_DWORD28.
  auto common_config_analog_dword_28 =
      tgl_registers::DekelCommonConfigAnalogDword28::GetForDdi(ddi_id()).ReadFrom(mmio_space_);
  common_config_analog_dword_28.set_reg_value(0x14158888)
      .WriteTo(mmio_space_)
      .ReadFrom(mmio_space_);  // Posting read

  // Step 3.2. Program rate dependent registers for Native and Alt DP mode.
  //
  // Register value table:
  // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev 2.0, Pages 191
  auto high_speed_clock_control =
      tgl_registers::DekelPllClktop2HighSpeedClockControl::GetForDdi(ddi_id()).ReadFrom(
          mmio_space_);
  auto core_clock_control =
      tgl_registers::DekelPllClktop2CoreClockControl1::GetForDdi(ddi_id()).ReadFrom(mmio_space_);

  switch (state.dp_bit_rate_mhz) {
    case 8100: {
      high_speed_clock_control.set_reg_value(0x0000011D);
      core_clock_control.set_reg_value(0x10080510);
      break;
    }
    case 5400: {
      high_speed_clock_control.set_reg_value(0x0000121D);
      core_clock_control.set_reg_value(0x10080510);
      break;
    }
    case 2700: {
      high_speed_clock_control.set_reg_value(0x0000521D);
      core_clock_control.set_reg_value(0x10080A12);
      break;
    }
    case 1620: {
      high_speed_clock_control.set_reg_value(0x0000621D);
      core_clock_control.set_reg_value(0x10080A12);
      break;
    }
    default: {
      zxlogf(ERROR, "Unsupported DP bit rate: %u MHz", state.dp_bit_rate_mhz);
      return false;
    }
  }

  // Program CLKTOP2_HSCLKCTL.
  high_speed_clock_control.WriteTo(mmio_space_).ReadFrom(mmio_space_);  // Posting read

  // Program CLKTOP2_CORECLKCTL1.
  core_clock_control.WriteTo(mmio_space_).ReadFrom(mmio_space_);  // Posting read

  // Step 5. If the frequency will result in a change to the voltage
  // requirement, follow the "Display Voltage Frequency Switching - Sequence
  // Before Frequency Change."
  //
  // TODO(fxbug.dev/98533): Currently it is okay to ignore this, unless we need
  // to support 5K+ display where we need to change display voltage and Core
  // Display Clock.

  // Step 6. Enable PLL in MGPLL_ENABLE.
  pll_enable.ReadFrom(mmio_space_).set_enable_dpll(1).WriteTo(mmio_space_);

  // Step 7. Wait for PLL lock status in MGPLL_ENABLE.
  // - Timeout and fail after 900us.
  if (!PollUntil([&] { return pll_enable.ReadFrom(mmio_space_).pll_is_locked(); }, zx::usec(1),
                 900)) {
    zxlogf(ERROR, "Dekel PLL (%s): Cannot enable PLL", name().c_str());
    return false;
  }

  // Step 8. If the frequency will result in a change to the voltage
  // requirement, follow the "Display Voltage Frequency Switching - Sequence
  // After Frequency Change".
  //
  // TODO(fxbug.dev/98533): Currently it is okay to ignore this, unless we need
  // to support 5K+ display where we need to change display voltage and Core
  // Display Clock.

  // 9. Program DDI_CLK_SEL to map the Type-C PLL clock to the port.
  auto ddi_clk_sel = tgl_registers::TypeCDdiClockSelect::GetForDdi(ddi_id()).ReadFrom(mmio_space_);
  ddi_clk_sel.set_clock_select(tgl_registers::TypeCDdiClockSelect::ClockSelect::kTypeCPll)
      .WriteTo(mmio_space_);

  // 10. Configure DPCLKA_CFGCR0 to turn on the clock for the port.
  auto dpclka_cfgcr0 = tgl_registers::DdiClockConfigControlRegister0::Get().ReadFrom(mmio_space_);
  dpclka_cfgcr0.turn_off_clock_for_ddi(ddi_id(), /*turn_off=*/false)
      .WriteTo(mmio_space_)
      .ReadFrom(mmio_space_);  // Posting read
  return true;
}

DpllManagerTigerLake::DpllManagerTigerLake(fdf::MmioBuffer* mmio_space) : mmio_space_(mmio_space) {
  constexpr std::array kDekelDplls = {
      tgl_registers::Dpll::DPLL_TC_1, tgl_registers::Dpll::DPLL_TC_2,
      tgl_registers::Dpll::DPLL_TC_3, tgl_registers::Dpll::DPLL_TC_4,
      tgl_registers::Dpll::DPLL_TC_5, tgl_registers::Dpll::DPLL_TC_6,
  };
  for (const auto dpll : kDekelDplls) {
    plls_[dpll] = std::make_unique<DekelPllTigerLake>(mmio_space_, dpll);
    ref_count_[plls_[dpll].get()] = 0;
  }
  // TODO(fxbug.dev/105240): Add COMBO PLLs (DPLL 0, 1, 4) to the `plls_` map.
  // TODO(fxbug.dev/99980): Add Thunderbolt PLL (DPLL 2) to the `plls_` map.

  // Load reference clock frequency.
  auto dssm = tgl_registers::Dssm::Get().ReadFrom(mmio_space_);
  switch (dssm.GetRefFrequency()) {
    case tgl_registers::Dssm::RefFrequency::k19_2Mhz:
      reference_clock_khz_ = 19'200;
      break;
    case tgl_registers::Dssm::RefFrequency::k24Mhz:
      reference_clock_khz_ = 24'000;
      break;
    case tgl_registers::Dssm::RefFrequency::k38_4Mhz:
      reference_clock_khz_ = 38'400;
      break;
    default:
      // Unreachable
      ZX_ASSERT_MSG(false, "DSSM: Invalid reference frequency field: 0x%x", dssm.ref_frequency());
  }
}

bool DpllManagerTigerLake::MapImpl(tgl_registers::Ddi ddi, tgl_registers::Dpll dpll) {
  switch (dpll) {
    case tgl_registers::Dpll::DPLL_TC_1:
    case tgl_registers::Dpll::DPLL_TC_2:
    case tgl_registers::Dpll::DPLL_TC_3:
    case tgl_registers::Dpll::DPLL_TC_4:
    case tgl_registers::Dpll::DPLL_TC_5:
    case tgl_registers::Dpll::DPLL_TC_6: {
      return ddi >= tgl_registers::Ddi::DDI_TC_1 && ddi <= tgl_registers::Ddi::DDI_TC_6 &&
             ddi - tgl_registers::Ddi::DDI_TC_1 == dpll - tgl_registers::Dpll::DPLL_TC_1;
    }
    case tgl_registers::Dpll::DPLL_0:
    case tgl_registers::Dpll::DPLL_1:
    case tgl_registers::Dpll::DPLL_2:
      // TODO(fxbug.dev/95863): Not implemented yet.
      zxlogf(ERROR, "MapImpl for DPLL %d not implemented", dpll);
      return false;
    default:
      return false;
  }
}

bool DpllManagerTigerLake::UnmapImpl(tgl_registers::Ddi ddi) { return true; }

std::optional<DpllState> DpllManagerTigerLake::LoadTypeCPllState(tgl_registers::Ddi ddi) {
  ZX_ASSERT(ddi >= tgl_registers::Ddi::DDI_TC_1);
  ZX_ASSERT(ddi <= tgl_registers::Ddi::DDI_TC_6);

  // TODO(fxbug.dev/99980): Currently this method assume all Type-C PHYs use
  // USB-C (Dekel PLL) instead of Thunderbolt. This needs to be changed once
  // we support Thunderbolt.

  // Follow the "Calculating PLL Frequency from Divider Values" algorithm
  // to calculate the output frequency of the PLL.
  // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev 2.0, Page 193
  //             Section "Calculating PLL Frequency from Divider Values"
  auto pll_divisor0 = tgl_registers::DekelPllDivisor0::GetForDdi(ddi).ReadFrom(mmio_space_);
  auto pll_bias = tgl_registers::DekelPllBias::GetForDdi(ddi).ReadFrom(mmio_space_);
  auto high_speed_clock_control =
      tgl_registers::DekelPllClktop2HighSpeedClockControl::GetForDdi(ddi).ReadFrom(mmio_space_);

  // M1 (feedback predivider) = DKL_PLL_DIV0[i_fbprediv_3_0]
  const int64_t m1_feedback_predivider = pll_divisor0.feedback_predivider_ratio();

  // M2 (feedback divider) = m2_integer_part + m2_fractional_part_bits / 2^22.
  const int64_t m2_feedback_divider_integer_part = pll_divisor0.feedback_divider_integer_part();
  const int64_t m2_feedback_divider_fractional_part_bits =
      pll_bias.fractional_modulator_enabled() ? pll_bias.feedback_divider_fractional_part_22_bits()
                                              : 0;

  // DIV1 (high speed divisor) = DKL_CLKTOP2_HSCLKCTL[od_clktop_hsdiv_divratio]
  const int64_t div1_high_speed_divisor = high_speed_clock_control.high_speed_divider_ratio();

  // DIV2 (programmable divisor) = DKL_CLKTOP2_HSCLKCTL[od_clktop_dsdiv_divratio]
  const int64_t div2_programmable_divisor = high_speed_clock_control.programmable_divider_ratio();

  // Symbol clock frequency
  // = M1 * M2 * reference frequency / ( 5 * div1 * div2 )
  // = M1 * (m2_integer_part + m2_fractional_part_bits / 2^22) * reference frequency / ( 5 * div1 *
  // div2 )
  // = (M1 * m2_integer_part * reference frequency + M1 * m2_fractional_part_bits * reference
  // frequency / 2^22) / (5 * div1 * div2);
  const int64_t symbol_rate_khz =
      (m1_feedback_predivider * m2_feedback_divider_integer_part * reference_clock_khz_ +
       ((m1_feedback_predivider * m2_feedback_divider_fractional_part_bits *
         reference_clock_khz_) >>
        22)) /
      (5ul * div1_high_speed_divisor * div2_programmable_divisor);

  // PLL output frequency (rate) is 5x the symbol rate.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev 2.0 "Type-C PLLs", Page 171
  const int64_t pll_out_rate_khz = symbol_rate_khz * 5;

  // Bit rate is 2x PLL output rate.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev 2.0 "Type-C PLLs", Page 171
  const int64_t bit_rate_khz = pll_out_rate_khz * 2;

  // Match calculated bit rate to valid DisplayPort bit rates.
  //
  // Valid DisplayPort link bit rates are:
  // - 1.62 GHz
  // - 2.7 GHz
  // - 5.4 GHz
  // - 8.1 GHz
  // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev 2.0 "Type-C PLLs", Page 171

  // TODO(fxbug.dev/109368): Currently we just assume all Type-C PHYs use DP Alt
  // mode, and only match the calculated bit rate to DisplayPort bit rates.
  // It could also be configured to use legacy HDMI / DVI, in which case the
  // symbol rate will fail to match any of the candidates and fail.
  constexpr int64_t kEpsilonKhz = 50'000;
  constexpr int64_t kValidDisplayPortBitRatesKhz[] = {1'620'000, 2'700'000, 5'400'000, 8'100'000};

  for (const auto valid_dp_bit_rate_khz : kValidDisplayPortBitRatesKhz) {
    if (abs(bit_rate_khz - valid_dp_bit_rate_khz) < kEpsilonKhz) {
      return DpDpllState{
          .dp_bit_rate_mhz = static_cast<uint32_t>(valid_dp_bit_rate_khz / 1000),
      };
    }
  }

  zxlogf(WARNING, "LoadTypeCPllState: DDI %d has invalid DisplayPort bit rate: %ld KHz", ddi,
         bit_rate_khz);
  return std::nullopt;
}

std::optional<DpllState> DpllManagerTigerLake::LoadState(tgl_registers::Ddi ddi) {
  switch (ddi) {
    case tgl_registers::Ddi::DDI_TC_1:
    case tgl_registers::Ddi::DDI_TC_2:
    case tgl_registers::Ddi::DDI_TC_3:
    case tgl_registers::Ddi::DDI_TC_4:
    case tgl_registers::Ddi::DDI_TC_5:
    case tgl_registers::Ddi::DDI_TC_6:
      return LoadTypeCPllState(ddi);

    case tgl_registers::Ddi::DDI_A:
    case tgl_registers::Ddi::DDI_B:
    case tgl_registers::Ddi::DDI_C:
      // TODO(fxbug.dev/105240): support loading PLL state from COMBO DDIs.
      return std::nullopt;
  }
}

DisplayPll* DpllManagerTigerLake::FindBestDpll(tgl_registers::Ddi ddi, bool is_edp,
                                               const DpllState& state) {
  DisplayPll* res = nullptr;

  // TODO(fxbug.dev/99980): Currently we assume `ddi` is always in DisplayPort
  // Alt mode. We need to map `ddi` to Thunderbolt DPLL once we support
  // Thunderbolt.
  if (ddi >= tgl_registers::Ddi::DDI_TC_1 && ddi <= tgl_registers::Ddi::DDI_TC_6) {
    auto dpll = TypeCDdiToDekelPll(ddi);
    res = plls_[dpll].get();
    zxlogf(TRACE, "FindBestDpll: select DPLL %s for DDI %d", res->name().c_str(), ddi);
    return res;
  }

  // TODO(fxbug.dev/105240): support COMBO DDIs.
  zxlogf(ERROR, "Unsupported DDI: %d", ddi);
  return nullptr;
}

}  // namespace i915_tgl
