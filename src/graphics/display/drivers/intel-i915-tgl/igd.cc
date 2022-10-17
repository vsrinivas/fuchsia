// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/igd.h"

#include <lib/device-protocol/pci.h>
#include <lib/zircon-internal/align.h>
#include <lib/zx/status.h>
#include <limits.h>
#include <zircon/status.h>

#include <hwreg/bitfields.h>

#include "src/graphics/display/drivers/intel-i915-tgl/acpi-memory-region.h"
#include "src/graphics/display/drivers/intel-i915-tgl/intel-i915-tgl.h"

namespace {

// Register definitions from IGD OpRegion/Software SCI documentation. Section
// numbers reference Skylake Sept 2016 rev 0.5.

// The number of eDP panel types supported by the IGD API
static const uint32_t kNumPanelTypes = 16;

// GMCH SWSCI Register - 5.1.1
class GmchSwsciRegister : public hwreg::RegisterBase<GmchSwsciRegister, uint16_t> {
 public:
  DEF_BIT(15, sci_event_select);
  DEF_BIT(0, gmch_sw_sci_trigger);

  static auto Get() { return hwreg::RegisterAddr<GmchSwsciRegister>(0); }
};

// Entry half of Software SCI Entry/Exit Parameters - 3.3.1
class SciEntryParam : public hwreg::RegisterBase<SciEntryParam, uint32_t> {
 public:
  DEF_RSVDZ_FIELD(31, 16);
  DEF_FIELD(15, 8, subfunction);
  DEF_RSVDZ_FIELD(7, 5);
  DEF_FIELD(4, 1, function);
  DEF_BIT(0, swsci_indicator);

  // Main function codes
  static const uint32_t kFuncGetBiosData = 4;

  // GetBiosData sub-function codes
  static const uint32_t kGbdaSupportedCalls = 0;
  static const uint32_t kGbdaPanelDetails = 5;

  static auto Get() { return hwreg::RegisterAddr<SciEntryParam>(0); }
};

// Exit half of Software SCI Entry/Exit Parameters - 3.3.1
class SciExitParam : public hwreg::RegisterBase<SciExitParam, uint32_t> {
 public:
  DEF_RSVDZ_FIELD(31, 16);
  DEF_FIELD(15, 8, exit_param);
  DEF_FIELD(7, 5, exit_result);
  DEF_RSVDZ_FIELD(4, 1);
  DEF_BIT(0, swsci_indicator);

  constexpr static uint32_t kResultOk = 1;

  static auto Get() { return hwreg::RegisterAddr<SciExitParam>(0); }
};

// Additional param return value for GetBiosData supported calls function - 4.2.2
class GbdaSupportedCalls : public hwreg::RegisterBase<GbdaSupportedCalls, uint32_t> {
 public:
  DEF_RSVDZ_FIELD(31, 11);
  DEF_BIT(10, get_aksv);
  DEF_BIT(9, spread_spectrum_clocks);
  DEF_RSVDZ_FIELD(8, 7);
  DEF_BIT(6, internal_graphics);
  DEF_BIT(5, tv_std_video_connector_info);
  DEF_BIT(4, get_panel_details);
  DEF_BIT(3, get_boot_display_preference);
  DEF_RSVDZ_FIELD(2, 1);
  DEF_BIT(0, requested_system_callbacks);

  static auto Get() { return hwreg::RegisterAddr<GbdaSupportedCalls>(0); }
};

// Additional param return value for GetBiosData panel details function - 4.2.5
class GbdaPanelDetails : public hwreg::RegisterBase<GbdaPanelDetails, uint32_t> {
 public:
  DEF_RSVDZ_FIELD(31, 23);
  DEF_FIELD(22, 20, bia_ctrl);
  DEF_FIELD(19, 18, blc_support);
  DEF_RSVDZ_BIT(17);
  DEF_BIT(16, lid_state);
  DEF_FIELD(15, 8, panel_type_plus1);
  DEF_FIELD(7, 0, panel_scaling);

  static auto Get() { return hwreg::RegisterAddr<GbdaPanelDetails>(0); }
};

static uint8_t iboost_idx_to_level(uint8_t iboost_idx) {
  switch (iboost_idx) {
    case 0:
      return 1;
    case 1:
      return 3;
    case 2:
      return 7;
    default:
      zxlogf(INFO, "Invalid iboost override");
      return 0;
  }
}

bool IsPortHdmi(uint8_t dvo_port) {
  switch (dvo_port) {
    case 0:   // DVO_PORT_HDMIA
    case 1:   // DVO_PORT_HDMIB
    case 2:   // DVO_PORT_HDMIC
    case 3:   // DVO_PORT_HDMID
    case 12:  // DVO_PORT_HDMIE
    case 14:  // DVO_PORT_HDMIF
    case 16:  // DVO_PORT_HDMIG
    case 18:  // DVO_PORT_HDMIH
    case 20:  // DVO_PORT_HDMII
      return true;
    default:
      return false;
  }
}

bool IsPortDisplayPort(uint8_t dvo_port) {
  switch (dvo_port) {
    case 10:  // DVO_PORT_DPA
    case 7:   // DVO_PORT_DPB
    case 8:   // DVO_PORT_DPC
    case 9:   // DVO_PORT_DPD
    case 11:  // DVO_PORT_DPE
    case 13:  // DVO_PORT_DPF
    case 15:  // DVO_PORT_DPG
    case 17:  // DVO_PORT_DPH
    case 19:  // DVO_PORT_DPI
      return true;
    default:
      return false;
  }
}

std::optional<tgl_registers::Ddi> PortToDdi(uint8_t dvo_port) {
  switch (dvo_port) {
    case 0:   // DVO_PORT_HDMIA
    case 10:  // DVO_PORT_DPA
      return tgl_registers::DDI_A;
    case 1:  // DVO_PORT_HDMIB
    case 7:  // DVO_PORT_DPB
      return tgl_registers::DDI_B;
    case 2:  // DVO_PORT_HDMIC
    case 8:  // DVO_PORT_DPC
      return tgl_registers::DDI_C;
    case 3:  // DVO_PORT_HDMID
    case 9:  // DVO_PORT_DPD
      // i.e. DDI_TC_1
      return tgl_registers::DDI_D;
    case 12:  // DVO_PORT_HDMIE
    case 11:  // DVO_PORT_DPE
      // i.e. DDI_TC_2
      return tgl_registers::DDI_E;
    case 14:  // DVO_PORT_HDMIF
    case 13:  // DVO_PORT_DPF
      return tgl_registers::DDI_TC_3;
    case 16:  // DVO_PORT_HDMIG
    case 15:  // DVO_PORT_DPG
      return tgl_registers::DDI_TC_4;
    case 18:  // DVO_PORT_HDMIH
    case 17:  // DVO_PORT_DPH
      return tgl_registers::DDI_TC_5;
    case 20:  // DVO_PORT_HDMII
    case 19:  // DVO_PORT_DPI
      return tgl_registers::DDI_TC_6;
    default:
      return std::nullopt;
  }
}

}  // namespace

namespace i915_tgl {

IgdOpRegion::~IgdOpRegion() = default;

template <typename T>
T* IgdOpRegion::GetSection(uint16_t* size) {
  return reinterpret_cast<T*>(GetSection(T::kBlockType, size));
}

uint8_t* IgdOpRegion::GetSection(uint8_t type, uint16_t* size) {
  uint8_t* data = reinterpret_cast<uint8_t*>(bdb_);
  uint16_t idx = bdb_->header_size;

  while (idx < bdb_->bios_data_blocks_size - sizeof(block_header_t)) {
    block_header_t* header = reinterpret_cast<block_header_t*>(data + idx);
    uint16_t block_size = static_cast<uint16_t>(header->size_low | (header->size_high << 8));
    if (block_size > bdb_->bios_data_blocks_size) {
      return nullptr;
    }
    uint16_t new_idx = static_cast<uint16_t>(idx + block_size + sizeof(block_header_t));
    if (new_idx <= bdb_->bios_data_blocks_size && header->type == type) {
      *size = block_size;
      return data + idx + sizeof(block_header_t);
    }
    idx = new_idx;
  }

  return nullptr;
}

bool IgdOpRegion::ProcessDdiConfigs() {
  uint16_t size;
  general_definitions_t* defs = GetSection<general_definitions_t>(&size);
  if (defs == nullptr) {
    zxlogf(ERROR, "Couldn't find vbt general definitions");
    return false;
  }
  if (size < sizeof(general_definitions_t)) {
    zxlogf(ERROR, "Bad size in vbt general definitions");
    return false;
  }
  uint16_t num_configs =
      static_cast<uint16_t>((size - sizeof(general_definitions_t)) / defs->ddi_config_size);
  for (int i = 0; i < num_configs; i++) {
    ddi_config_t* cfg = reinterpret_cast<ddi_config_t*>(defs->ddis + i * defs->ddi_config_size);
    if (!cfg->ddi_flags) {
      continue;
    }

    auto ddi_flags = DdiFlags::Get().FromValue(cfg->ddi_flags);
    if (IsPortHdmi(cfg->port_type)) {
      if (!ddi_flags.tmds()) {
        zxlogf(WARNING, "Malformed hdmi config");
        continue;
      }
    } else if (IsPortDisplayPort(cfg->port_type)) {
      if (!ddi_flags.dp()) {
        zxlogf(WARNING, "Malformed dp config");
        continue;
      }
    } else {
      zxlogf(WARNING, "The port %d is not supported, ignored.", cfg->port_type);
      continue;
    }

    auto ddi = PortToDdi(cfg->port_type);
    ZX_DEBUG_ASSERT(ddi.has_value());

    if (ddi_features_.find(*ddi) != ddi_features_.end()) {
      zxlogf(WARNING, "Duplicate ddi config");
      continue;
    }

    ddi_features_[*ddi] = {
        .supports_hdmi = ddi_flags.tmds() && !ddi_flags.not_hdmi(),
        .supports_dvi = static_cast<bool>(ddi_flags.tmds()),
        .supports_dp = static_cast<bool>(ddi_flags.dp()),
        .is_edp = ddi_flags.dp() && ddi_flags.internal(),
        .is_type_c = static_cast<bool>(cfg->is_usb_type_c()),
        .is_thunderbolt = (bdb_->version >= 209) ? static_cast<bool>(cfg->is_thunderbolt()) : false,
        .iboosts =
            {
                .hdmi_iboost = cfg->has_iboost_override()
                                   ? iboost_idx_to_level(cfg->dp_iboost_override())
                                   : static_cast<uint8_t>(0),
                .dp_iboost = cfg->has_iboost_override()
                                 ? iboost_idx_to_level(cfg->hdmi_iboost_override())
                                 : static_cast<uint8_t>(0),
            },
        .hdmi_buffer_translation_idx = cfg->ddi_buf_trans_idx(),
    };
  }

  return true;
}

bool IgdOpRegion::Swsci(const ddk::Pci& pci, uint16_t function, uint16_t subfunction,
                        uint32_t additional_param, uint16_t* exit_param, uint32_t* additional_res) {
  uint16_t val;
  if (pci.ReadConfig16(kIgdSwSciReg, &val) != ZX_OK) {
    zxlogf(WARNING, "Failed to read SWSCI register");
    return false;
  }
  auto gmch_swsci_reg = GmchSwsciRegister::Get().FromValue(val);
  if (!gmch_swsci_reg.sci_event_select() || gmch_swsci_reg.gmch_sw_sci_trigger()) {
    zxlogf(WARNING, "Bad GMCH SWSCI register value (%04x)", val);
    return false;
  }

  sci_interface_protocol_t* sci_interface =
      reinterpret_cast<sci_interface_protocol_t*>(igd_opregion_->mailbox2);

  auto sci_entry_param = SciEntryParam::Get().FromValue(0);
  sci_entry_param.set_function(function);
  sci_entry_param.set_subfunction(subfunction);
  sci_entry_param.set_swsci_indicator(1);
  sci_interface->entry_and_exit_params = sci_entry_param.reg_value();
  sci_interface->additional_params = additional_param;

  if (pci.WriteConfig16(kIgdSwSciReg, gmch_swsci_reg.set_gmch_sw_sci_trigger(1).reg_value()) !=
      ZX_OK) {
    zxlogf(WARNING, "Failed to write SWSCI register");
    return false;
  }

  // The spec says to wait for 2ms if driver_sleep_timeout isn't set, but that's not
  // long enough. I've seen delays as long as 10ms, so use 50ms to be safe.
  int timeout_ms = sci_interface->driver_sleep_timeout ? sci_interface->driver_sleep_timeout : 50;
  while (timeout_ms-- > 0) {
    auto sci_exit_param = SciExitParam::Get().FromValue(sci_interface->entry_and_exit_params);
    if (!sci_exit_param.swsci_indicator()) {
      if (sci_exit_param.exit_result() == SciExitParam::kResultOk) {
        *exit_param = static_cast<uint16_t>(sci_exit_param.exit_param());
        *additional_res = sci_interface->additional_params;
        return true;
      } else {
        zxlogf(WARNING, "SWSCI failed (%x)", sci_exit_param.exit_result());
        return false;
      }
    }
    zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
  }
  zxlogf(WARNING, "SWSCI timeout");
  return false;
}

bool IgdOpRegion::GetPanelType(const ddk::Pci& pci, uint8_t* type) {
  uint16_t exit_param;
  uint32_t additional_res;
  // TODO(stevensd): cache the supported calls when we need to use Swsci more than once
  if (Swsci(pci, SciEntryParam::kFuncGetBiosData, SciEntryParam::kGbdaSupportedCalls,
            0 /* unused additional_param */, &exit_param, &additional_res)) {
    auto support = GbdaSupportedCalls::Get().FromValue(additional_res);
    if (support.get_panel_details()) {
      // TODO(stevensd): Support the case where there is >1 eDP panel
      uint32_t panel_number = 0;
      if (Swsci(pci, SciEntryParam::kFuncGetBiosData, SciEntryParam::kGbdaPanelDetails,
                panel_number, &exit_param, &additional_res)) {
        auto details = GbdaPanelDetails::Get().FromValue(additional_res);
        if (details.panel_type_plus1() && details.panel_type_plus1() < (kNumPanelTypes + 1)) {
          *type = static_cast<uint8_t>(details.panel_type_plus1() - 1);
          zxlogf(DEBUG, "SWSCI panel type %d", *type);
          return true;
        }
      }
    }
  }

  uint16_t size;
  lvds_config_t* cfg = GetSection<lvds_config_t>(&size);
  if (!cfg || cfg->panel_type >= kNumPanelTypes) {
    return false;
  }
  *type = cfg->panel_type;

  return true;
}

bool IgdOpRegion::CheckForLowVoltageEdp(const ddk::Pci& pci) {
  bool has_edp = true;
  for (const auto& kv : ddi_features_) {
    has_edp |= kv.second.is_edp;
  }
  if (!has_edp) {
    zxlogf(DEBUG, "No edp found");
    return true;
  }

  uint16_t size;
  edp_config_t* edp = GetSection<edp_config_t>(&size);
  if (edp == nullptr) {
    zxlogf(WARNING, "Couldn't find edp general definitions");
    return false;
  }

  if (!GetPanelType(pci, &panel_type_)) {
    zxlogf(TRACE, "No panel type");
    return false;
  }
  edp_is_low_voltage_ =
      !((edp->vswing_preemphasis[panel_type_ / 2] >> (4 * panel_type_ % 2)) & 0xf);

  zxlogf(TRACE, "Is low voltage edp? %d", edp_is_low_voltage_);

  return true;
}

void IgdOpRegion::ProcessBacklightData() {
  uint16_t size;
  lfp_backlight_t* data = GetSection<lfp_backlight_t>(&size);

  if (data) {
    lfp_backlight_entry_t* e = &data->entries[panel_type_];
    min_backlight_brightness_ = e->min_brightness / 255.0;
  }
}

zx_status_t IgdOpRegion::Init(const ddk::Pci& pci) {
  uint32_t igd_addr;
  zx_status_t status = pci.ReadConfig32(kIgdOpRegionAddrReg, &igd_addr);
  if (status != ZX_OK || !igd_addr) {
    zxlogf(ERROR, "Failed to locate IGD OpRegion (%d)", status);
    return status;
  }

  {
    zx::result<AcpiMemoryRegion> memory_op_region =
        AcpiMemoryRegion::Create(igd_addr, kIgdOpRegionLen);
    if (memory_op_region.is_error()) {
      zxlogf(ERROR, "Failed to map IGD Memory OpRegion: %s",
             zx_status_get_string(memory_op_region.error_value()));
      return memory_op_region.error_value();
    }

    memory_op_region_ = std::move(memory_op_region).value();
    igd_opregion_ = reinterpret_cast<igd_opregion_t*>(memory_op_region_.data().data());
    if (!igd_opregion_->validate()) {
      zxlogf(ERROR, "Failed to validate IGD Memory OpRegion");
      return ZX_ERR_INTERNAL;
    }
  }

  vbt_header_t* vbt_header = nullptr;

  if (igd_opregion_->major_version() == 2 && igd_opregion_->minor_version() == 1 &&
      igd_opregion_->asle_supported()) {
    auto [rvda, rvds] = igd_opregion_->vbt_region();

    zx::result<AcpiMemoryRegion> extended_vbt_region =
        AcpiMemoryRegion::Create(igd_addr + rvda, rvds);
    if (extended_vbt_region.is_error()) {
      zxlogf(ERROR, "Failed to map extended VBT: %s",
             zx_status_get_string(extended_vbt_region.error_value()));
      return extended_vbt_region.error_value();
    }

    extended_vbt_region_ = std::move(extended_vbt_region).value();
    vbt_header = reinterpret_cast<vbt_header_t*>(extended_vbt_region_.data().data());
  } else {
    vbt_header = reinterpret_cast<vbt_header_t*>(&igd_opregion_->mailbox4);
  }

  if (!vbt_header->validate()) {
    zxlogf(ERROR, "Failed to validate vbt header");
    return ZX_ERR_INTERNAL;
  }

  bdb_ = reinterpret_cast<bios_data_blocks_header_t*>(reinterpret_cast<uintptr_t>(vbt_header) +
                                                      vbt_header->bios_data_blocks_offset);
  uint16_t vbt_size = vbt_header->vbt_size;
  if (!bdb_->validate() || bdb_->bios_data_blocks_size > vbt_size ||
      vbt_header->bios_data_blocks_offset + bdb_->bios_data_blocks_size > vbt_size) {
    zxlogf(ERROR, "Failed to validate bdb header");
    return ZX_ERR_INTERNAL;
  }

  // TODO(stevensd): 196 seems old enough that all gen9 processors will have it. If we want to
  // support older hardware, we'll need to handle missing data.
  if (bdb_->version < 196) {
    zxlogf(ERROR, "Out of date vbt (%d)", bdb_->version);
    return ZX_ERR_INTERNAL;
  }

  if (!ProcessDdiConfigs() || !CheckForLowVoltageEdp(pci)) {
    return ZX_ERR_INTERNAL;
  }

  ProcessBacklightData();

  return ZX_OK;
}

}  // namespace i915_tgl
