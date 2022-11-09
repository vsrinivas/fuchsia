// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/dpll.h"

#include <lib/ddk/debug.h>
#include <lib/zx/time.h>
#include <zircon/assert.h>

#include <memory>
#include <optional>
#include <tuple>

#include "src/graphics/display/drivers/intel-i915-tgl/dpll-config.h"
#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"
#include "src/graphics/display/drivers/intel-i915-tgl/poll-until.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-ddi.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-dpll.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-typec.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers.h"

namespace i915_tgl {

bool DdiPllConfig::IsValid() const {
  if (ddi_clock_khz <= 0) {
    return false;
  }
  if (!admits_display_port && !admits_hdmi) {
    return false;
  }
  return true;
}

bool operator==(const DdiPllConfig& lhs, const DdiPllConfig& rhs) noexcept {
  return std::tie(lhs.ddi_clock_khz, lhs.spread_spectrum_clocking, lhs.admits_display_port,
                  lhs.admits_hdmi) == std::tie(rhs.ddi_clock_khz, rhs.spread_spectrum_clocking,
                                               rhs.admits_display_port, rhs.admits_hdmi);
}

bool operator!=(const DdiPllConfig& lhs, const DdiPllConfig& rhs) noexcept { return !(lhs == rhs); }

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

}  // namespace

DisplayPll::DisplayPll(tgl_registers::Dpll dpll) : dpll_(dpll), name_(GetDpllName(dpll)) {}

DisplayPll* DisplayPllManager::SetDdiPllConfig(DdiId ddi_id, bool is_edp,
                                               const DdiPllConfig& desired_config) {
  zxlogf(TRACE, "Configuring PLL for DDI %d - SSC %s, DDI clock %d kHz, DisplayPort %s, HDMI %s",
         ddi_id, desired_config.spread_spectrum_clocking ? "yes" : "no",
         desired_config.ddi_clock_khz, desired_config.admits_display_port ? "yes" : "no",
         desired_config.admits_hdmi ? "yes" : "no");

  // Asserting after zxlogf() facilitates debugging, because the invalid
  // configuration will be captured in the log.
  ZX_ASSERT(desired_config.IsValid());

  const auto ddi_to_dpll_it = ddi_to_dpll_.find(ddi_id);
  if (ddi_to_dpll_it != ddi_to_dpll_.end()) {
    DisplayPll* pll = ddi_to_dpll_it->second;
    if (pll->config() == desired_config) {
      zxlogf(WARNING, "SetDdiPllConfig() will unnecessarily reset the PLL for DDI %d", ddi_id);
    }
    ResetDdiPll(ddi_id);
  }

  DisplayPll* best_dpll = FindPllFor(ddi_id, is_edp, desired_config);
  if (!best_dpll) {
    zxlogf(ERROR, "Failed to allocate DPLL to DDI %d - %d kHz %s DisplayPort: %s HDMI: %s", ddi_id,
           desired_config.ddi_clock_khz, desired_config.spread_spectrum_clocking ? "SSC" : "no SSC",
           desired_config.admits_display_port ? "yes" : "no",
           desired_config.admits_hdmi ? "yes" : "no");
    return nullptr;
  }
  zxlogf(DEBUG, "Assigning DPLL %s to DDI %d - %d kHz %s DisplayPort: %s HDMI: %s",
         best_dpll->name().c_str(), ddi_id, desired_config.ddi_clock_khz,
         desired_config.spread_spectrum_clocking ? "SSC" : "no SSC",
         desired_config.admits_display_port ? "yes" : "no",
         desired_config.admits_hdmi ? "yes" : "no");

  if (ref_count_[best_dpll] > 0 || best_dpll->Enable(desired_config)) {
    if (!SetDdiClockSource(ddi_id, best_dpll->dpll())) {
      zxlogf(ERROR, "Failed to map DDI %d to DPLL (%s)", ddi_id, best_dpll->name().c_str());
      return nullptr;
    }
    ref_count_[best_dpll]++;
    ddi_to_dpll_[ddi_id] = best_dpll;
    return best_dpll;
  }
  return nullptr;
}

bool DisplayPll::Enable(const DdiPllConfig& pll_config) {
  zxlogf(TRACE, "Configuring PLL %d: SSC %s, DDI clock %d kHz, DisplayPort %s, HDMI %s", dpll(),
         pll_config.spread_spectrum_clocking ? "yes" : "no", pll_config.ddi_clock_khz,
         pll_config.admits_display_port ? "yes" : "no", pll_config.admits_hdmi ? "yes" : "no");

  // Asserting after zxlogf() facilitates debugging, because the invalid
  // configuration will be captured in the log.
  ZX_ASSERT(pll_config.IsValid());

  if (!config_.IsEmpty()) {
    zxlogf(ERROR, "Enable(): PLL %s already enabled!", name().c_str());
    return false;
  }

  const bool success = DoEnable(pll_config);
  if (success) {
    config_ = pll_config;
    zxlogf(TRACE, "Enabled DPLL %d: SSC %s, DDI clock %d kHz, DisplayPort %s, HDMI %s", dpll(),
           pll_config.spread_spectrum_clocking ? "yes" : "no", pll_config.ddi_clock_khz,
           pll_config.admits_display_port ? "yes" : "no", pll_config.admits_hdmi ? "yes" : "no");

  } else {
    zxlogf(ERROR, "Failed to enable DPLL %d: SSC %s, DDI clock %d kHz, DisplayPort %s, HDMI %s",
           dpll(), pll_config.spread_spectrum_clocking ? "yes" : "no", pll_config.ddi_clock_khz,
           pll_config.admits_display_port ? "yes" : "no", pll_config.admits_hdmi ? "yes" : "no");
  }
  return success;
}

bool DisplayPll::Disable() {
  zxlogf(TRACE, "Disabling PLL %d", dpll());
  if (config_.IsEmpty()) {
    zxlogf(INFO, "DoDisable(): PLL %s already disabled", name().c_str());
    return true;
  }
  const bool success = DoDisable();

  if (success) {
    config_ = {};
    zxlogf(TRACE, "Disabled PLL %d", dpll());
  } else {
    zxlogf(ERROR, "Failed to disable PLL %d", dpll());
  }
  return success;
}

bool DisplayPllManager::ResetDdiPll(DdiId ddi_id) {
  if (ddi_to_dpll_.find(ddi_id) == ddi_to_dpll_.end()) {
    return true;
  }

  DisplayPll* dpll = ddi_to_dpll_[ddi_id];
  if (!ResetDdiClockSource(ddi_id)) {
    zxlogf(ERROR, "Failed to unmap DPLL (%s) for DDI %d", dpll->name().c_str(), ddi_id);
    return false;
  }

  ZX_DEBUG_ASSERT(ref_count_[dpll] > 0);
  if (--ref_count_[dpll] == 0) {
    ddi_to_dpll_.erase(ddi_id);
    return dpll->Disable();
  }
  return true;
}

bool DisplayPllManager::DdiPllMatchesConfig(DdiId ddi_id, const DdiPllConfig& desired_config) {
  const auto ddi_to_dpll_it = ddi_to_dpll_.find(ddi_id);
  if (ddi_to_dpll_it == ddi_to_dpll_.end()) {
    return true;
  }
  return ddi_to_dpll_it->second->config() != desired_config;
}

DpllSkylake::DpllSkylake(fdf::MmioBuffer* mmio_space, tgl_registers::Dpll dpll)
    : DisplayPll(dpll), mmio_space_(mmio_space) {}

bool DpllSkylake::DoEnable(const DdiPllConfig& pll_config) {
  // This implements the common steps in the sections "DisplayPort Programming"
  // > "DisplayPort PLL Enable Sequence" and "HDMI and DVI PLL Enable
  // Sequence" in the display engine PRMs.
  //
  // The specifics of each section are implemented in ConfigureForHdmi() and
  // ConfigureForDisplayPort(), which contain full references to the PRMs.

  bool configure_success;
  if (pll_config.admits_hdmi) {
    configure_success = ConfigureForHdmi(pll_config);
  } else {
    ZX_DEBUG_ASSERT(pll_config.admits_display_port);
    configure_success = ConfigureForDisplayPort(pll_config);
  }
  if (!configure_success) {
    return false;
  }

  auto dpll_enable = tgl_registers::PllEnable::GetForSkylakeDpll(dpll()).ReadFrom(mmio_space_);
  dpll_enable.set_pll_enabled(true).WriteTo(mmio_space_);
  if (!PollUntil(
          [&] {
            return tgl_registers::DisplayPllStatus::Get().ReadFrom(mmio_space_).pll_locked(dpll());
          },
          zx::msec(1), 5)) {
    zxlogf(ERROR, "Skylake DPLL %d failed to lock after 5ms!", dpll());
    return false;
  }

  return true;
}

bool DpllSkylake::ConfigureForDisplayPort(const DdiPllConfig& pll_config) {
  // This implements the "DisplayPort Programming" > "DisplayPort PLL Enable
  // Sequence" section in the display engine PRMs.
  //
  // Kaby Lake: IHD-OS-KBL-Vol 12-1.17 page 133
  // Skylake: IHD-OS-SKL-Vol 12-05.16 page 130

  const int32_t display_port_link_rate_mhz = pll_config.ddi_clock_khz / 500;
  zxlogf(TRACE, "Configuring Skylake DPLL %d: DisplayPort, link rate %d Mbps", dpll(),
         display_port_link_rate_mhz);

  auto dpll_control1 = tgl_registers::DisplayPllControl1::Get().ReadFrom(mmio_space_);
  const int16_t ddi_clock_mhz = static_cast<int16_t>(pll_config.ddi_clock_khz / 1'000);
  dpll_control1.set_pll_uses_hdmi_configuration_mode(dpll(), false)
      .set_pll_spread_spectrum_clocking_enabled(dpll(), false)
      .set_pll_display_port_ddi_frequency_mhz(dpll(), ddi_clock_mhz)
      .set_pll_programming_enabled(dpll(), true)
      .WriteTo(mmio_space_);

  // The PRM instructs us to read back the configuration register in order to
  // ensure that the writes completed. This must happen before enabling the PLL.
  dpll_control1.ReadFrom(mmio_space_);

  return true;
}

bool DpllSkylake::ConfigureForHdmi(const DdiPllConfig& pll_config) {
  ZX_ASSERT_MSG(dpll() != tgl_registers::Dpll::DPLL_0, "DPLL 0 only supports DisplayPort DDIs");

  // This implements the "HDMI and DVI Programming" > "HDMI and DVI PLL Enable
  // Sequence" section in the display engine PRMs.
  //
  // Kaby Lake: IHD-OS-KBL-Vol 12-1.17 page 134
  // Skylake: IHD-OS-SKL-Vol 12-05.16 page 131

  const DpllOscillatorConfig dco_config =
      CreateDpllOscillatorConfigKabyLake(pll_config.ddi_clock_khz);
  if (dco_config.frequency_divider == 0) {
    return false;
  }

  const DpllFrequencyDividerConfig divider_config =
      CreateDpllFrequencyDividerConfigKabyLake(dco_config.frequency_divider);

  zxlogf(TRACE, "Configuring DPLL %d: HDMI DCO frequency=%d dividers P*Q*K=%u*%u*%u Center=%u Mhz",
         dpll(), dco_config.frequency_khz, divider_config.p0_p_divider, divider_config.p1_q_divider,
         divider_config.p2_k_divider, dco_config.center_frequency_khz);

  auto dpll_control1 = tgl_registers::DisplayPllControl1::Get().ReadFrom(mmio_space_);
  dpll_control1.set_pll_uses_hdmi_configuration_mode(dpll(), true)
      .set_pll_spread_spectrum_clocking_enabled(dpll(), false)
      .set_pll_programming_enabled(dpll(), true)
      .WriteTo(mmio_space_);

  auto dpll_config1 =
      tgl_registers::DisplayPllDcoFrequencyKabyLake::GetForDpll(dpll()).FromValue(0);
  dpll_config1.set_frequency_programming_enabled(true)
      .set_dco_frequency_khz(dco_config.frequency_khz)
      .WriteTo(mmio_space_);

  auto dpll_config2 = tgl_registers::DisplayPllDcoDividersKabyLake::GetForDpll(dpll()).FromValue(0);
  dpll_config2.set_q_p1_divider(divider_config.p1_q_divider)
      .set_k_p2_divider(divider_config.p2_k_divider)
      .set_p_p0_divider(divider_config.p0_p_divider)
      .set_center_frequency_mhz(static_cast<int16_t>(dco_config.center_frequency_khz / 1'000))
      .WriteTo(mmio_space_);

  // The PRM instructs us to read back the configuration registers in order to
  // ensure that the writes completed. This must happen before enabling the PLL.
  dpll_control1.ReadFrom(mmio_space_);
  dpll_config1.ReadFrom(mmio_space_);
  dpll_config2.ReadFrom(mmio_space_);
  return true;
}

bool DpllSkylake::DoDisable() {
  // We must not disable DPLL0 here, because it also drives the core display
  // clocks (CDCLK, CD2XCLK). DPLL0 must only get disabled during display engine
  // un-initialization.
  if (dpll() != tgl_registers::DPLL_0) {
    auto dpll_enable = tgl_registers::PllEnable::GetForSkylakeDpll(dpll()).ReadFrom(mmio_space_);
    dpll_enable.set_pll_enabled(false).WriteTo(mmio_space_);
  }
  return true;
}

DpllManagerSkylake::DpllManagerSkylake(fdf::MmioBuffer* mmio_space) : mmio_space_(mmio_space) {
  for (const auto dpll : tgl_registers::Dplls<tgl_registers::Platform::kSkylake>()) {
    plls_[dpll] = std::make_unique<DpllSkylake>(mmio_space, dpll);
    ref_count_[plls_[dpll].get()] = 0;
  }
}

bool DpllManagerSkylake::SetDdiClockSource(DdiId ddi_id, tgl_registers::Dpll pll) {
  auto dpll_ddi_map = tgl_registers::DisplayPllDdiMapKabyLake::Get().ReadFrom(mmio_space_);
  dpll_ddi_map.set_ddi_clock_programming_enabled(ddi_id, true)
      .set_ddi_clock_disabled(ddi_id, false)
      .set_ddi_clock_display_pll(ddi_id, pll)
      .WriteTo(mmio_space_);

  return true;
}

bool DpllManagerSkylake::ResetDdiClockSource(DdiId ddi_id) {
  auto dpll_ddi_map = tgl_registers::DisplayPllDdiMapKabyLake::Get().ReadFrom(mmio_space_);
  dpll_ddi_map.set_ddi_clock_disabled(ddi_id, true).WriteTo(mmio_space_);

  return true;
}

DdiPllConfig DpllManagerSkylake::LoadState(DdiId ddi_id) {
  auto dpll_ddi_map = tgl_registers::DisplayPllDdiMapKabyLake::Get().ReadFrom(mmio_space_);
  if (dpll_ddi_map.ddi_clock_disabled(ddi_id)) {
    zxlogf(TRACE, "Loaded DDI %d DPLL state: DDI clock disabled", ddi_id);
    return DdiPllConfig{};
  }

  const tgl_registers::Dpll dpll = dpll_ddi_map.ddi_clock_display_pll(ddi_id);
  auto dpll_enable = tgl_registers::PllEnable::GetForSkylakeDpll(dpll).ReadFrom(mmio_space_);
  if (!dpll_enable.pll_enabled()) {
    zxlogf(TRACE, "Loaded DDI %d DPLL %d state: DPLL disabled", ddi_id, dpll);
    return DdiPllConfig{};
  }

  // Remove stale mappings first.
  if (ddi_to_dpll_.find(ddi_id) != ddi_to_dpll_.end()) {
    ZX_DEBUG_ASSERT(ref_count_.find(ddi_to_dpll_[ddi_id]) != ref_count_.end());
    ZX_DEBUG_ASSERT(ref_count_.at(ddi_to_dpll_[ddi_id]) > 0);
    --ref_count_[ddi_to_dpll_[ddi_id]];
    ddi_to_dpll_.erase(ddi_id);
  }

  ddi_to_dpll_[ddi_id] = plls_[dpll].get();
  ++ref_count_[ddi_to_dpll_[ddi_id]];

  auto dpll_control1 = tgl_registers::DisplayPllControl1::Get().ReadFrom(mmio_space_);
  const bool uses_hdmi_mode = dpll_control1.pll_uses_hdmi_configuration_mode(dpll);
  if (uses_hdmi_mode) {
    auto dpll_dco_frequency =
        tgl_registers::DisplayPllDcoFrequencyKabyLake::GetForDpll(dpll).ReadFrom(mmio_space_);
    auto dpll_dco_dividers =
        tgl_registers::DisplayPllDcoDividersKabyLake::GetForDpll(dpll).ReadFrom(mmio_space_);

    // P (P0) and K (P2) are <= 7, so their product fits in int8_t.
    const int16_t dco_frequency_divider =
        (dpll_dco_dividers.p_p0_divider() * dpll_dco_dividers.k_p2_divider()) *
        int16_t{dpll_dco_dividers.q_p1_divider()};

    const int32_t ddi_clock_khz =
        static_cast<int32_t>(dpll_dco_frequency.dco_frequency_khz() / dco_frequency_divider);

    zxlogf(TRACE,
           "Loaded DDI %d DPLL %d state: HDMI no SSC DCO frequency=%d kHz divider P*Q*K=%u*%u*%u "
           "Center=%u Mhz",
           ddi_id, dpll, dpll_dco_frequency.dco_frequency_khz(), dpll_dco_dividers.p_p0_divider(),
           dpll_dco_dividers.q_p1_divider(), dpll_dco_dividers.k_p2_divider(),
           dpll_dco_dividers.center_frequency_mhz());

    // TODO(fxbug.com/112752): The DpllSkylake instance is not updated to
    //                         reflect the state in the registers.
    return DdiPllConfig{
        .ddi_clock_khz = ddi_clock_khz,
        .spread_spectrum_clocking = false,
        .admits_display_port = false,
        .admits_hdmi = true,
    };
  }

  const int16_t ddi_frequency_mhz = dpll_control1.pll_display_port_ddi_frequency_mhz(dpll);
  if (ddi_frequency_mhz == 0) {
    zxlogf(ERROR, "DPLL %d has invalid DisplayPort DDI clock. DPLL_CTRL1 value: %x", dpll,
           dpll_control1.reg_value());
    return DdiPllConfig{};
  }

  const int32_t ddi_clock_khz = ddi_frequency_mhz * 1'000;
  const bool spread_spectrum_clocking = dpll_control1.pll_spread_spectrum_clocking_enabled(dpll);

  zxlogf(TRACE, "Loaded DDI %d DPLL %d state: DisplayPort %s %d kHz (link rate %d Mbps)", ddi_id,
         dpll, spread_spectrum_clocking ? "SSC" : "no SSC", ddi_clock_khz, ddi_frequency_mhz * 2);

  // TODO(fxbug.com/112752): The DpllSkylake instance is not updated to reflect
  //                         the state in the registers.
  return DdiPllConfig{
      .ddi_clock_khz = ddi_clock_khz,
      .spread_spectrum_clocking = spread_spectrum_clocking,
      .admits_display_port = true,
      .admits_hdmi = false,
  };
}

DisplayPll* DpllManagerSkylake::FindPllFor(DdiId ddi_id, bool is_edp,
                                           const DdiPllConfig& desired_config) {
  if (is_edp) {
    ZX_DEBUG_ASSERT(desired_config.admits_display_port);

    DisplayPll* pll0 = plls_[tgl_registers::DPLL_0].get();
    if (ref_count_[pll0] == 0 || pll0->config() == desired_config) {
      return pll0;
    }
  } else {
    DisplayPll* const kCandidates[] = {
        plls_[tgl_registers::Dpll::DPLL_1].get(),
        plls_[tgl_registers::Dpll::DPLL_3].get(),
        plls_[tgl_registers::Dpll::DPLL_2].get(),
    };
    for (DisplayPll* candidate : kCandidates) {
      if (ref_count_[candidate] == 0 || candidate->config() == desired_config) {
        return candidate;
      }
    }
  }
  return nullptr;
}

DisplayPllTigerLake::DisplayPllTigerLake(fdf::MmioBuffer* mmio_space, tgl_registers::Dpll dpll)
    : DisplayPll(dpll), mmio_space_(mmio_space) {}

bool DisplayPllTigerLake::DoEnable(const DdiPllConfig& pll_config) {
  // This implements the "DisplayPort Combo PHY Programming" > "DisplayPort Mode
  // PLL Enable Sequence" section in the display engine PRMs. The "HDMI Mode PLL
  // Enable Sequence" is documented to be identical, modulo SSC ability.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev 2.0 pages 177-178

  DpllOscillatorConfig dco_config =
      pll_config.admits_hdmi
          ? CreateDpllOscillatorConfigForHdmiTigerLake(pll_config.ddi_clock_khz)
          : CreateDpllOscillatorConfigForDisplayPortTigerLake(pll_config.ddi_clock_khz);
  if (dco_config.frequency_divider == 0) {
    return false;
  }

  const DpllFrequencyDividerConfig divider_config =
      CreateDpllFrequencyDividerConfigTigerLake(dco_config.frequency_divider);
  zxlogf(TRACE, "Configuring PLL %d: DCO frequency=%d dividers P*Q*K=%u*%u*%u Center=%u Mhz",
         dpll(), dco_config.frequency_khz, divider_config.p0_p_divider, divider_config.p1_q_divider,
         divider_config.p2_k_divider, dco_config.center_frequency_khz);

  auto dpll_enable = tgl_registers::PllEnable::GetForTigerLakeDpll(dpll()).ReadFrom(mmio_space_);
  dpll_enable.set_power_on_request_tiger_lake(true);
  dpll_enable.WriteTo(mmio_space_);
  if (!PollUntil([&] { return dpll_enable.ReadFrom(mmio_space_).powered_on_tiger_lake(); },
                 zx::msec(1), 5)) {
    zxlogf(ERROR, "DPLL %d power up failure!", dpll());
    return false;
  }

  auto display_straps = tgl_registers::DisplayStraps::Get().ReadFrom(mmio_space_);
  const int32_t reference_clock_khz = display_straps.reference_frequency_khz_tiger_lake();

  auto pll_dco_frequency =
      tgl_registers::DisplayPllDcoFrequencyTigerLake::GetForDpll(dpll()).FromValue(0);
  pll_dco_frequency.set_dco_frequency_khz(dco_config.frequency_khz, reference_clock_khz)
      .WriteTo(mmio_space_);

  auto pll_dco_dividers =
      tgl_registers::DisplayPllDcoDividersTigerLake::GetForDpll(dpll()).FromValue(0);
  pll_dco_dividers.set_q_p1_divider(divider_config.p1_q_divider)
      .set_k_p2_divider(divider_config.p2_k_divider)
      .set_p_p0_divider(divider_config.p0_p_divider)
      .WriteTo(mmio_space_);

  // The PRM instructs us to read back any configuration register in order to
  // ensure that the writes completed. This must happen before enabling the PLL.
  pll_dco_dividers.ReadFrom(mmio_space_);

  dpll_enable.set_pll_enabled(true);
  dpll_enable.WriteTo(mmio_space_);
  if (!PollUntil([&] { return dpll_enable.pll_locked_tiger_lake_and_lcpll1(); }, zx::usec(1),
                 500)) {
    zxlogf(ERROR, "DPLL %d lock failure! Failed to lock after 500us", dpll());
    return false;
  }

  set_config(pll_config);
  return true;
}

bool DisplayPllTigerLake::DoDisable() {
  auto pll_enable = tgl_registers::PllEnable::GetForTigerLakeDpll(dpll()).ReadFrom(mmio_space_);
  pll_enable.set_pll_enabled(false).WriteTo(mmio_space_);
  return true;
}

namespace {

tgl_registers::Dpll TypeCDdiToDekelPll(DdiId type_c_ddi) {
  switch (type_c_ddi) {
    case DdiId::DDI_TC_1:
      return tgl_registers::Dpll::DPLL_TC_1;
    case DdiId::DDI_TC_2:
      return tgl_registers::Dpll::DPLL_TC_2;
    case DdiId::DDI_TC_3:
      return tgl_registers::Dpll::DPLL_TC_3;
    case DdiId::DDI_TC_4:
      return tgl_registers::Dpll::DPLL_TC_4;
    case DdiId::DDI_TC_5:
      return tgl_registers::Dpll::DPLL_TC_5;
    case DdiId::DDI_TC_6:
      return tgl_registers::Dpll::DPLL_TC_6;
    default:
      ZX_ASSERT_MSG(false, "Not a Type-C DDI");
  }
}

DdiId DekelPllToTypeCDdi(tgl_registers::Dpll dekel_pll) {
  switch (dekel_pll) {
    case tgl_registers::Dpll::DPLL_TC_1:
      return DdiId::DDI_TC_1;
    case tgl_registers::Dpll::DPLL_TC_2:
      return DdiId::DDI_TC_2;
    case tgl_registers::Dpll::DPLL_TC_3:
      return DdiId::DDI_TC_3;
    case tgl_registers::Dpll::DPLL_TC_4:
      return DdiId::DDI_TC_4;
    case tgl_registers::Dpll::DPLL_TC_5:
      return DdiId::DDI_TC_5;
    case tgl_registers::Dpll::DPLL_TC_6:
      return DdiId::DDI_TC_6;
    default:
      ZX_ASSERT_MSG(false, "Not a Dekel PLL");
  }
}

}  // namespace

DekelPllTigerLake::DekelPllTigerLake(fdf::MmioBuffer* mmio_space, tgl_registers::Dpll dpll)
    : DisplayPll(dpll), mmio_space_(mmio_space) {}

DdiId DekelPllTigerLake::ddi_id() const {
  ZX_DEBUG_ASSERT(dpll() >= tgl_registers::DPLL_TC_1);
  ZX_DEBUG_ASSERT(dpll() <= tgl_registers::DPLL_TC_6);
  return DekelPllToTypeCDdi(dpll());
}

bool DekelPllTigerLake::DoEnable(const DdiPllConfig& pll_config) {
  if (pll_config.admits_hdmi) {
    return EnableHdmi(pll_config);
  }
  return EnableDp(pll_config);
}

bool DekelPllTigerLake::DoDisable() {
  // Follow the "DKL PLL Disable Sequence" to disable the PLL.
  //
  // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev2.0, Pages 188-189
  //             "DKL PLL Disable Sequence"

  // Step 1. Configure DPCLKA_CFGCR0 to turn off the clock for the port.
  auto ddi_clock_config = tgl_registers::DdiClockConfig::Get().ReadFrom(mmio_space_);
  ddi_clock_config.set_ddi_clock_disabled(ddi_id(), true).WriteTo(mmio_space_);
  ddi_clock_config.ReadFrom(mmio_space_);  // Posting read

  // Step 2. If the frequency will result in a change to the voltage requirement,
  // follow the "Display Voltage Frequency Switching - Sequence Before Frequency
  // Change".
  //
  // TODO(fxbug.dev/98533): Currently it is okay to ignore this, unless we need
  // to support 5K+ display where we need to change display voltage and Core
  // Display Clock.

  // 3. Disable PLL through MGPLL_ENABLE.
  auto pll_enable = tgl_registers::PllEnable::GetForTigerLakeDpll(dpll()).ReadFrom(mmio_space_);
  pll_enable.ReadFrom(mmio_space_).set_pll_enabled(false).WriteTo(mmio_space_);

  // Step 4. Wait for PLL not locked status in MGPLL_ENABLE.
  // Should complete within 50us.
  if (!PollUntil(
          [&] { return !pll_enable.ReadFrom(mmio_space_).pll_locked_tiger_lake_and_lcpll1(); },
          zx::usec(1), 50)) {
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
  pll_enable.set_power_on_request_tiger_lake(false).WriteTo(mmio_space_);

  // 7. Wait for PLL power state disabled in MGPLL_ENABLE.
  // - Should complete immediately.
  if (!PollUntil([&] { return !pll_enable.ReadFrom(mmio_space_).powered_on_tiger_lake(); },
                 zx::usec(1), 10)) {
    zxlogf(ERROR, "Dekel PLL %s: Cannot disable PLL power", name().c_str());
  }

  return true;
}

bool DekelPllTigerLake::EnableHdmi(const DdiPllConfig& pll_config) {
  // TODO(fxbug.dev/109368): Support HDMI on Type-C.
  zxlogf(ERROR, "Dekel PLL %s: EnableHdmi: Not implemented", name().c_str());
  return false;
}

bool DekelPllTigerLake::EnableDp(const DdiPllConfig& pll_config) {
  // This method contains the procedure to enable DisplayPort Mode Dekel PLL.
  // Reference:
  // Tiger Lake: Section "DKL PLL Enable Sequence",
  //             IHD-OS-TGL-Vol 12-1.22-Rev 2.0, Pages 177-178

  auto pll_enable = tgl_registers::PllEnable::GetForTigerLakeDpll(dpll()).ReadFrom(mmio_space_);
  pll_enable.set_power_on_request_tiger_lake(true).WriteTo(mmio_space_);
  if (!PollUntil([&] { return pll_enable.ReadFrom(mmio_space_).powered_on_tiger_lake(); },
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

  const int display_port_link_rate_mbps = pll_config.ddi_clock_khz / 500;
  switch (display_port_link_rate_mbps) {
    case 8'100: {
      high_speed_clock_control.set_reg_value(0x0000011D);
      core_clock_control.set_reg_value(0x10080510);
      break;
    }
    case 5'400: {
      high_speed_clock_control.set_reg_value(0x0000121D);
      core_clock_control.set_reg_value(0x10080510);
      break;
    }
    case 2'700: {
      high_speed_clock_control.set_reg_value(0x0000521D);
      core_clock_control.set_reg_value(0x10080A12);
      break;
    }
    case 1'620: {
      high_speed_clock_control.set_reg_value(0x0000621D);
      core_clock_control.set_reg_value(0x10080A12);
      break;
    }
    default: {
      zxlogf(ERROR, "Unsupported DP link rate: %d Mbps", display_port_link_rate_mbps);
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
  pll_enable.ReadFrom(mmio_space_).set_pll_enabled(true).WriteTo(mmio_space_);

  // Step 7. Wait for PLL lock status in MGPLL_ENABLE.
  // - Timeout and fail after 900us.
  if (!PollUntil(
          [&] { return pll_enable.ReadFrom(mmio_space_).pll_locked_tiger_lake_and_lcpll1(); },
          zx::usec(1), 900)) {
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
  auto ddi_clock_config = tgl_registers::DdiClockConfig::Get().ReadFrom(mmio_space_);
  ddi_clock_config.set_ddi_clock_disabled(ddi_id(), false).WriteTo(mmio_space_);
  ddi_clock_config.ReadFrom(mmio_space_);  // Posting read
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

  constexpr std::array kDisplayPllIds = {
      tgl_registers::Dpll::DPLL_0, tgl_registers::Dpll::DPLL_1,
      // TODO(fxbug.dev/110351): Add support for DPLL4.
  };
  for (const tgl_registers::Dpll display_pll_id : kDisplayPllIds) {
    const auto [plls_it, success] = plls_.try_emplace(
        display_pll_id, std::make_unique<DisplayPllTigerLake>(mmio_space_, display_pll_id));
    ZX_DEBUG_ASSERT_MSG(success, "Display PLL %d already inserted in map", display_pll_id);
  }

  // TODO(fxbug.dev/99980): Add Thunderbolt PLL (DPLL 2) to the `plls_` map.

  auto display_straps = tgl_registers::DisplayStraps::Get().ReadFrom(mmio_space_);
  reference_clock_khz_ = display_straps.reference_frequency_khz_tiger_lake();
}

bool DpllManagerTigerLake::SetDdiClockSource(DdiId ddi_id, tgl_registers::Dpll pll) {
  switch (pll) {
    case tgl_registers::Dpll::DPLL_TC_1:
    case tgl_registers::Dpll::DPLL_TC_2:
    case tgl_registers::Dpll::DPLL_TC_3:
    case tgl_registers::Dpll::DPLL_TC_4:
    case tgl_registers::Dpll::DPLL_TC_5:
    case tgl_registers::Dpll::DPLL_TC_6: {
      ZX_ASSERT(ddi_id >= DdiId::DDI_TC_1);
      ZX_ASSERT(ddi_id <= DdiId::DDI_TC_6);
      ZX_ASSERT(ddi_id - DdiId::DDI_TC_1 == pll - tgl_registers::Dpll::DPLL_TC_1);
      return true;
    }

    case tgl_registers::Dpll::DPLL_0:
    case tgl_registers::Dpll::DPLL_1: {
      if (ddi_id < DdiId::DDI_A || ddi_id > DdiId::DDI_C) {
        return false;
      }
      auto dpll_clock_config = tgl_registers::DdiClockConfig::Get().ReadFrom(mmio_space_);
      dpll_clock_config.set_ddi_clock_disabled(ddi_id, false)
          .set_ddi_clock_display_pll(ddi_id, pll)
          .WriteTo(mmio_space_);
      return true;
    }

    case tgl_registers::Dpll::DPLL_2:
      // TODO(fxbug.dev/99980): Thunderbolt support.
      zxlogf(ERROR, "SetDdiClockSource() does not support DPLL 2 (for Thunderbolt) yet");
      return false;

    default:
      zxlogf(ERROR, "SetDdiClockSource() does not support DPLL %d yet", pll);
      return false;
  }
}

bool DpllManagerTigerLake::ResetDdiClockSource(DdiId ddi_id) {
  if (ddi_id >= DdiId::DDI_TC_1 && ddi_id <= DdiId::DDI_TC_6) {
    // TODO(fxbug.dev/99980): Any configuration needed if the DDI uses DPLL 2
    // (Display PLL 2, dedicated to Thunderbolt frequencies)?

    return true;
  }

  ZX_DEBUG_ASSERT(ddi_id >= DdiId::DDI_A);
  ZX_DEBUG_ASSERT(ddi_id <= DdiId::DDI_C);
  auto dpll_clock_config = tgl_registers::DdiClockConfig::Get().ReadFrom(mmio_space_);
  dpll_clock_config.set_ddi_clock_disabled(ddi_id, true).WriteTo(mmio_space_);
  return true;
}

DdiPllConfig DpllManagerTigerLake::LoadStateForComboDdi(DdiId ddi_id) {
  ZX_ASSERT(ddi_id >= DdiId::DDI_A);
  ZX_ASSERT(ddi_id <= DdiId::DDI_C);

  auto ddi_clock_config = tgl_registers::DdiClockConfig::Get().ReadFrom(mmio_space_);
  if (ddi_clock_config.ddi_clock_disabled(ddi_id)) {
    zxlogf(TRACE, "Loaded DDI %d DPLL config: DDI clock disabled", ddi_id);
    return DdiPllConfig{};
  }

  const tgl_registers::Dpll dpll = ddi_clock_config.ddi_clock_display_pll(ddi_id);
  if (dpll == tgl_registers::DPLL_INVALID) {
    zxlogf(WARNING,
           "Invalid DDI %d DPLL config: Invalid clock source DPLL! DDI Clock Config register: %x",
           ddi_id, ddi_clock_config.reg_value());
    return DdiPllConfig{};
  }
  if (dpll == tgl_registers::DPLL_2) {
    zxlogf(WARNING,
           "Invalid DDI %d DPLL config: clock source is DPLL 2, but DPLL2 reserved for Thunderbot.",
           ddi_id);
    return DdiPllConfig{};
  }

  auto dpll_enable = tgl_registers::PllEnable::GetForTigerLakeDpll(dpll).ReadFrom(mmio_space_);
  if (!dpll_enable.pll_enabled()) {
    zxlogf(TRACE, "Loaded DDI %d DPLL %d config: DPLL disabled", ddi_id, dpll);
    return DdiPllConfig{};
  }

  // We don't currently have enough documentation to configure the DPLL divider
  // register. However, since the field breakdown is documented, we can log it,
  // in case it helps any future investigation.
  auto dpll_divider = tgl_registers::DisplayPllDivider::GetForDpll(dpll).ReadFrom(mmio_space_);
  zxlogf(
      TRACE,
      "Loaded DDI %d DPLL %d dividers: early lock %d, true lock %d, AFC start point %d, "
      "feedback clock retiming %s, loop filter - integral 2^(-%d) proportional 2^(1-%d) gain 2^%d "
      "pre-divider %d, post-divider (M2) %d",
      ddi_id, dpll, dpll_divider.early_lock_criteria_cycles(),
      dpll_divider.true_lock_criteria_cycles(),
      dpll_divider.automatic_frequency_calibration_start_point(),
      dpll_divider.feedback_clock_retiming_enabled() ? "yes" : "no",
      dpll_divider.loop_filter_integral_coefficient_exponent(),
      dpll_divider.loop_filter_proportional_coefficient_exponent(),
      dpll_divider.loop_filter_gain_control(), dpll_divider.feedback_pre_divider(),
      dpll_divider.feedback_post_divider());

  auto dpll_dco_frequency =
      tgl_registers::DisplayPllDcoFrequencyTigerLake::GetForDpll(dpll).ReadFrom(mmio_space_);
  auto dpll_dco_dividers =
      tgl_registers::DisplayPllDcoDividersTigerLake::GetForDpll(dpll).ReadFrom(mmio_space_);
  auto dpll_spread_spectrum_clocking =
      tgl_registers::DisplayPllSpreadSpectrumClocking::GetForDpll(dpll).ReadFrom(mmio_space_);

  if (dpll_dco_dividers.reference_clock_select() !=
      tgl_registers::DisplayPllDcoDividersTigerLake::ReferenceClockSelect::kDisplayReference) {
    zxlogf(
        ERROR,
        "Loaded DDI %d DPLL %d config: DPLL uses genlock clock reference %d. Genlock not supported!",
        ddi_id, dpll, dpll_dco_dividers.reference_clock_select());
    return DdiPllConfig{};
  }

  // Remove stale mappings first.
  if (ddi_to_dpll_.find(ddi_id) != ddi_to_dpll_.end()) {
    ZX_DEBUG_ASSERT(ref_count_.count(ddi_to_dpll_[ddi_id]) > 0);
    ZX_DEBUG_ASSERT(ref_count_[ddi_to_dpll_[ddi_id]] > 0);
    --ref_count_[ddi_to_dpll_[ddi_id]];
    ddi_to_dpll_.erase(ddi_id);
  }

  ddi_to_dpll_[ddi_id] = plls_[dpll].get();
  ++ref_count_[ddi_to_dpll_[ddi_id]];

  const int32_t dco_frequency_khz =
      dpll_dco_frequency.dco_frequency_khz(static_cast<int32_t>(reference_clock_khz_));

  // P (P0) and K (P2) are <= 7, so their product fits in int8_t.
  const int16_t dco_frequency_divider =
      (dpll_dco_dividers.p_p0_divider() * dpll_dco_dividers.k_p2_divider()) *
      int16_t{dpll_dco_dividers.q_p1_divider()};

  const int32_t ddi_clock_khz = static_cast<int32_t>(dco_frequency_khz / dco_frequency_divider);

  zxlogf(
      TRACE,
      "Loaded DDI %d DPLL %d config: %s DDI clock %d kHz DCO frequency=%d kHz divider P*Q*K=%u*%u*%u",
      ddi_id, dpll, dpll_spread_spectrum_clocking.enabled() ? "SSC" : "no SSC", ddi_clock_khz,
      dco_frequency_khz, dpll_dco_dividers.p_p0_divider(), dpll_dco_dividers.q_p1_divider(),
      dpll_dco_dividers.k_p2_divider());

  return DdiPllConfig{
      .ddi_clock_khz = ddi_clock_khz,
      .spread_spectrum_clocking = (dpll_spread_spectrum_clocking.enabled() != 0),
      .admits_display_port = true,
      .admits_hdmi = true,
  };
}

DdiPllConfig DpllManagerTigerLake::LoadStateForTypeCDdi(DdiId ddi_id) {
  ZX_ASSERT(ddi_id >= DdiId::DDI_TC_1);
  ZX_ASSERT(ddi_id <= DdiId::DDI_TC_6);

  // TODO(fxbug.dev/99980): Currently this method assume all Type-C PHYs use
  // USB-C (Dekel PLL) instead of Thunderbolt. This needs to be changed once
  // we support Thunderbolt.

  // Follow the "Calculating PLL Frequency from Divider Values" algorithm
  // to calculate the output frequency of the PLL.
  // Tiger Lake: IHD-OS-TGL-Vol 12-1.22-Rev 2.0, Page 193
  //             Section "Calculating PLL Frequency from Divider Values"
  auto pll_divisor0 = tgl_registers::DekelPllDivisor0::GetForDdi(ddi_id).ReadFrom(mmio_space_);
  auto pll_bias = tgl_registers::DekelPllBias::GetForDdi(ddi_id).ReadFrom(mmio_space_);
  auto high_speed_clock_control =
      tgl_registers::DekelPllClktop2HighSpeedClockControl::GetForDdi(ddi_id).ReadFrom(mmio_space_);

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
  static constexpr int64_t kEpsilonKhz = 50'000;
  static constexpr int64_t kValidDisplayPortBitRatesKhz[] = {1'620'000, 2'700'000, 5'400'000,
                                                             8'100'000};

  for (const auto valid_dp_bit_rate_khz : kValidDisplayPortBitRatesKhz) {
    if (abs(bit_rate_khz - valid_dp_bit_rate_khz) < kEpsilonKhz) {
      // TODO(fxbug.com/112752): The DekelPllTigerLake instance is not updated
      //                         to reflect the state in the registers.
      return DdiPllConfig{
          .ddi_clock_khz = static_cast<int16_t>(valid_dp_bit_rate_khz / 2),
          .spread_spectrum_clocking = false,
          .admits_display_port = true,
          .admits_hdmi = false,
      };
    }
  }

  zxlogf(WARNING, "LoadTypeCPllState: DDI %d has invalid DisplayPort bit rate: %ld KHz", ddi_id,
         bit_rate_khz);
  return DdiPllConfig{};
}

DdiPllConfig DpllManagerTigerLake::LoadState(DdiId ddi_id) {
  switch (ddi_id) {
    case DdiId::DDI_TC_1:
    case DdiId::DDI_TC_2:
    case DdiId::DDI_TC_3:
    case DdiId::DDI_TC_4:
    case DdiId::DDI_TC_5:
    case DdiId::DDI_TC_6:
      return LoadStateForTypeCDdi(ddi_id);

    case DdiId::DDI_A:
    case DdiId::DDI_B:
    case DdiId::DDI_C:
      return LoadStateForComboDdi(ddi_id);
  }
}

DisplayPll* DpllManagerTigerLake::FindPllFor(DdiId ddi_id, bool is_edp,
                                             const DdiPllConfig& desired_config) {
  // TODO(fxbug.dev/99980): Currently we assume `ddi` is always in DisplayPort
  // Alt mode. We need to map `ddi` to Thunderbolt DPLL once we support
  // Thunderbolt.
  if (ddi_id >= DdiId::DDI_TC_1 && ddi_id <= DdiId::DDI_TC_6) {
    auto dpll = TypeCDdiToDekelPll(ddi_id);
    return plls_[dpll].get();
  }

  constexpr std::array kDisplayPllIds = {
      tgl_registers::Dpll::DPLL_0, tgl_registers::Dpll::DPLL_1,
      // TODO(fxbug.dev/110351): Add support for DPLL4.
  };
  for (const tgl_registers::Dpll display_pll_id : kDisplayPllIds) {
    const auto plls_it = plls_.find(display_pll_id);
    ZX_DEBUG_ASSERT_MSG(plls_it != plls_.end(), "PLL %d not in map", display_pll_id);

    DisplayPll* const display_pll = plls_it->second.get();
    if (ref_count_[display_pll] == 0 || display_pll->config() == desired_config) {
      return display_pll;
    }
  }
  return nullptr;
}

}  // namespace i915_tgl
