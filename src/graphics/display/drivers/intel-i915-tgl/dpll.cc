// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/dpll.h"

#include <lib/zx/time.h>
#include <zircon/assert.h>

#include <optional>
#include <variant>

#include "src/graphics/display/drivers/intel-i915-tgl/intel-i915-tgl.h"
#include "src/graphics/display/drivers/intel-i915-tgl/poll-until.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-ddi.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-dpll.h"

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

std::optional<tgl_registers::DpllControl1::LinkRate> DpBitRateMhzToSklLinkRate(
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

std::optional<uint32_t> SklLinkRateToDpBitRateMhz(tgl_registers::DpllControl1::LinkRate link_rate) {
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

SklDpll::SklDpll(fdf::MmioBuffer* mmio_space, tgl_registers::Dpll dpll)
    : DisplayPll(dpll), mmio_space_(mmio_space) {}

bool SklDpll::Enable(const DpllState& state) {
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

bool SklDpll::EnableDp(const DpDpllState& dp_state) {
  // Configure this DPLL to produce a suitable clock signal.
  auto dpll_ctrl1 = tgl_registers::DpllControl1::Get().ReadFrom(mmio_space_);
  dpll_ctrl1.dpll_hdmi_mode(dpll()).set(0);
  dpll_ctrl1.dpll_ssc_enable(dpll()).set(0);
  auto dp_rate = DpBitRateMhzToSklLinkRate(dp_state.dp_bit_rate_mhz);
  if (!dp_rate.has_value()) {
    zxlogf(ERROR, "Invalid DP bit rate: %u MHz", dp_state.dp_bit_rate_mhz);
    return false;
  }
  dpll_ctrl1.SetLinkRate(dpll(), *dp_rate);
  dpll_ctrl1.dpll_override(dpll()).set(1);
  dpll_ctrl1.WriteTo(mmio_space_);
  dpll_ctrl1.ReadFrom(mmio_space_);  // Posting read

  // Enable this DPLL and wait for it to lock
  auto dpll_enable = tgl_registers::DpllEnable::Get(dpll()).ReadFrom(mmio_space_);
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

bool SklDpll::EnableHdmi(const HdmiDpllState& hdmi_state) {
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
  auto dpll_enable = tgl_registers::DpllEnable::Get(dpll()).ReadFrom(mmio_space_);
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

bool SklDpll::Disable() {
  if (!enabled_) {
    zxlogf(INFO, "Dpll %s Disable(): Already disabled", name().c_str());
    return true;
  }

  // We don't want to disable DPLL0, since that drives cdclk.
  if (dpll() != tgl_registers::DPLL_0) {
    auto dpll_enable = tgl_registers::DpllEnable::Get(dpll()).ReadFrom(mmio_space_);
    dpll_enable.set_enable_dpll(0);
    dpll_enable.WriteTo(mmio_space_);
  }
  enabled_ = false;

  return true;
}

SklDpllManager::SklDpllManager(fdf::MmioBuffer* mmio_space) : mmio_space_(mmio_space) {
  plls_.resize(tgl_registers::kDpllCount);
  constexpr std::array<tgl_registers::Dpll, 4> kSklDplls = {
      tgl_registers::Dpll::DPLL_0,
      tgl_registers::Dpll::DPLL_1,
      tgl_registers::Dpll::DPLL_2,
      tgl_registers::Dpll::DPLL_3,
  };

  for (const auto dpll : kSklDplls) {
    plls_[dpll] = std::unique_ptr<SklDpll>(new SklDpll(mmio_space, dpll));
    ref_count_[plls_[dpll].get()] = 0;
  }
}

bool SklDpllManager::MapImpl(tgl_registers::Ddi ddi, tgl_registers::Dpll dpll) {
  // Direct the DPLL to the DDI
  auto dpll_ctrl2 = tgl_registers::DpllControl2::Get().ReadFrom(mmio_space_);
  dpll_ctrl2.ddi_select_override(ddi).set(1);
  dpll_ctrl2.ddi_clock_off(ddi).set(0);
  dpll_ctrl2.ddi_clock_select(ddi).set(dpll);
  dpll_ctrl2.WriteTo(mmio_space_);

  return true;
}

bool SklDpllManager::UnmapImpl(tgl_registers::Ddi ddi) {
  auto dpll_ctrl2 = tgl_registers::DpllControl2::Get().ReadFrom(mmio_space_);
  dpll_ctrl2.ddi_clock_off(ddi).set(1);
  dpll_ctrl2.WriteTo(mmio_space_);

  return true;
}

std::optional<DpllState> SklDpllManager::LoadState(tgl_registers::Ddi ddi) {
  auto dpll_ctrl2 = tgl_registers::DpllControl2::Get().ReadFrom(mmio_space_);
  if (dpll_ctrl2.ddi_clock_off(ddi).get()) {
    return std::nullopt;
  }

  auto dpll = static_cast<tgl_registers::Dpll>(dpll_ctrl2.ddi_clock_select(ddi).get());
  auto dpll_enable = tgl_registers::DpllEnable::Get(dpll).ReadFrom(mmio_space_);
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
    auto dp_bit_rate_mhz = SklLinkRateToDpBitRateMhz(dpll_ctrl1.GetLinkRate(dpll));
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

DisplayPll* SklDpllManager::FindBestDpll(tgl_registers::Ddi ddi, bool is_edp,
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

}  // namespace i915_tgl
