// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <hwreg/bitfields.h>
#include <lib/zx/object.h>
#include <lib/zx/vmar.h>

#include "igd.h"
#include "intel-i915.h"
#include "macros.h"

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
        case 0: return 1;
        case 1: return 3;
        case 2: return 7;
        default:
            LOG_INFO("Invalid iboost override\n");
            return 0;
    }
}

} // namespace

namespace i915 {

IgdOpRegion::IgdOpRegion() {}

IgdOpRegion::~IgdOpRegion() {
    if (igd_opregion_pages_base_) {
        zx::vmar::root_self()->unmap(igd_opregion_pages_base_, igd_opregion_pages_len_);
    }
}

template<typename T> T* IgdOpRegion::GetSection(uint16_t* size) {
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
        LOG_ERROR("Couldn't find vbt general definitions\n");
        return false;
    }
    if (size < sizeof(general_definitions_t)) {
        LOG_ERROR("Bad size in vbt general definitions\n");
        return false;
    }
    uint16_t num_configs = static_cast<uint16_t>(
            (size - sizeof(general_definitions_t)) / defs->ddi_config_size);
    for (int i = 0; i < num_configs; i++) {
        ddi_config_t* cfg = reinterpret_cast<ddi_config_t*>(defs->ddis + i * defs->ddi_config_size);
        if (!cfg->ddi_flags) {
            continue;
        }

        auto ddi_flags = DdiFlags::Get().FromValue(cfg->ddi_flags);
        uint8_t idx;
        if (cfg->port_type < 4 || cfg->port_type == 12) {
            // Types 0, 1, 2, 3, and 12 are HDMI ports A, B, C, D, and E
            if (!ddi_flags.tmds()) {
                LOG_WARN("Malformed hdmi config\n");
                continue;
            }

            idx = cfg->port_type < 4 ? static_cast<registers::Ddi>(cfg->port_type)
                                     : registers::DDI_E;
        } else if (7 <= cfg->port_type && cfg->port_type <= 11) {
            // Types 7, 8, 9, 10, 11 are DP ports B, C, D, A, E
            if (!ddi_flags.dp()) {
                LOG_WARN("Malformed dp config\n");
                continue;
            }

            if (cfg->port_type <= 9) {
                idx = static_cast<uint8_t>(cfg->port_type - 6);
            } else if (cfg->port_type == 10) {
                idx = registers::DDI_A;
            } else {
                ZX_DEBUG_ASSERT(cfg->port_type == 11);
                idx = registers::DDI_E;
            }
        } else {
            continue;
        }

        if (ddi_supports_dvi_[idx] || ddi_supports_dp_[idx]) {
            LOG_WARN("Duplicate ddi config\n");
            continue;
        }
        ddi_supports_dvi_[idx] = ddi_flags.tmds();
        ddi_supports_hdmi_[idx] = ddi_flags.tmds() && !ddi_flags.not_hdmi();
        ddi_supports_dp_[idx] = ddi_flags.dp();
        ddi_is_edp_[idx] = ddi_flags.dp() && ddi_flags.internal();

        hdmi_buffer_translation_idx_[idx] = cfg->ddi_buf_trans_idx();
        if (cfg->has_iboost_override()) {
            iboosts_[idx].dp_iboost = iboost_idx_to_level(cfg->dp_iboost_override());
            iboosts_[idx].hdmi_iboost = iboost_idx_to_level(cfg->hdmi_iboost_override());
        } else {
            iboosts_[idx].dp_iboost = 0;
            iboosts_[idx].hdmi_iboost = 0;
        }
    }

    return true;
}

bool IgdOpRegion::Swsci(pci_protocol_t* pci,
                        uint16_t function, uint16_t subfunction, uint32_t additional_param,
                        uint16_t* exit_param, uint32_t* additional_res) {
    uint16_t val;
    if (pci_config_read16(pci, kIgdSwSciReg, &val) != ZX_OK) {
        LOG_WARN("Failed to read SWSCI register\n");
        return false;
    }
    auto gmch_swsci_reg = GmchSwsciRegister::Get().FromValue(val);
    if (!gmch_swsci_reg.sci_event_select() || gmch_swsci_reg.gmch_sw_sci_trigger()) {
        LOG_WARN("Bad GMCH SWSCI register value (%04x)\n", val);
        return false;
    }

    sci_interface_t* sci_interface = reinterpret_cast<sci_interface_t*>(igd_opregion_->mailbox2);

    auto sci_entry_param = SciEntryParam::Get().FromValue(0);
    sci_entry_param.set_function(function);
    sci_entry_param.set_subfunction(subfunction);
    sci_entry_param.set_swsci_indicator(1);
    sci_interface->entry_and_exit_params = sci_entry_param.reg_value();
    sci_interface->additional_params = additional_param;

    if (pci_config_write16(pci, kIgdSwSciReg,
                           gmch_swsci_reg.set_gmch_sw_sci_trigger(1).reg_value()) != ZX_OK) {
        LOG_WARN("Failed to write SWSCI register\n");
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
                LOG_WARN("SWSCI failed (%x)\n", sci_exit_param.exit_result());
                return false;
            }
        }
        zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
    }
    LOG_WARN("SWSCI timeout\n");
    return false;
}

bool IgdOpRegion::GetPanelType(pci_protocol_t* pci, uint8_t* type) {
    uint16_t exit_param;
    uint32_t additional_res;
    // TODO(stevensd): cache the supported calls when we nede to use Swsci more than once
    if (Swsci(pci, SciEntryParam::kFuncGetBiosData, SciEntryParam::kGbdaSupportedCalls,
              0 /* unused additional_param */, &exit_param, &additional_res)) {
        auto support = GbdaSupportedCalls::Get().FromValue(additional_res);
        if (support.get_panel_details()) {
            // TODO(stevensd): Support the case where there is >1 eDP panel
            uint32_t panel_number = 0;
            if (Swsci(pci, SciEntryParam::kFuncGetBiosData, SciEntryParam::kGbdaPanelDetails,
                      panel_number, &exit_param, &additional_res)) {
                auto details = GbdaPanelDetails::Get().FromValue(additional_res);
                if (details.panel_type_plus1()
                        && details.panel_type_plus1() < (kNumPanelTypes + 1)) {
                    *type = static_cast<uint8_t>(details.panel_type_plus1() - 1);
                    LOG_SPEW("SWSCI panel type %d\n", *type);
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

bool IgdOpRegion::CheckForLowVoltageEdp(pci_protocol_t* pci) {
    bool has_edp = true;
    for (unsigned i = 0; i < registers::kDdiCount; i++) {
        has_edp |= ddi_is_edp_[i];
    }
    if (!has_edp) {
        LOG_SPEW("No edp found\n");
        return true;
    }

    uint16_t size;
    edp_config_t* edp = GetSection<edp_config_t>(&size);
    if (edp == nullptr) {
        LOG_WARN("Couldn't find edp general definitions\n");
        return false;
    }

    if (!GetPanelType(pci, &panel_type_)) {
        LOG_TRACE("No panel type\n");
        return false;
    }
    edp_is_low_voltage_ =
            !((edp->vswing_preemphasis[panel_type_ / 2] >> (4 * panel_type_ % 2)) & 0xf);

    LOG_TRACE("Is low voltage edp? %d\n", edp_is_low_voltage_);

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

zx_status_t IgdOpRegion::Init(pci_protocol_t* pci) {
    uint32_t igd_addr;
    zx_status_t status = pci_config_read32(pci, kIgdOpRegionAddrReg, &igd_addr);
    if (status != ZX_OK || !igd_addr) {
        LOG_ERROR("Failed to locate IGD OpRegion (%d)\n", status);
        return status;
    }

    // TODO(stevensd): This is directly mapping a physical address into our address space, which
    // is not something we'll be able to do forever. At some point, there will need to be an
    // actual API (probably in ACPI) to do this.
    zx_handle_t vmo;
    uint32_t igd_opregion_pages_len_ = kIgdOpRegionLen + (igd_addr & PAGE_SIZE);
    status = zx_vmo_create_physical(get_root_resource(),
                                    igd_addr & ~(PAGE_SIZE - 1),
                                    igd_opregion_pages_len_, &vmo);
    if (status != ZX_OK) {
        LOG_ERROR("Failed to access IGD OpRegion (%d)\n", status);
        return status;
    }
    igd_opregion_pages_ = zx::vmo(vmo);

    status = zx::vmar::root_self()->map(0, igd_opregion_pages_, 0, igd_opregion_pages_len_,
                                       ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                                       &igd_opregion_pages_base_);
    if (status != ZX_OK) {
        LOG_ERROR("Failed to map IGD OpRegion (%d)\n", status);
        return status;
    }

    igd_opregion_ = reinterpret_cast<igd_opregion_t*>(
            igd_opregion_pages_base_ + (igd_addr % PAGE_SIZE));
    if (!igd_opregion_->validate()) {
        LOG_ERROR("Failed to validate IGD OpRegion\n");
        return ZX_ERR_INTERNAL;
    }

    vbt_header_t* vbt_header = reinterpret_cast<vbt_header_t*>(&igd_opregion_->mailbox4);
    if (!vbt_header->validate()) {
        LOG_ERROR("Failed to validate vbt header\n");
        return ZX_ERR_INTERNAL;
    }

    bdb_ = reinterpret_cast<bios_data_blocks_header_t*>(
            igd_opregion_->mailbox4 + vbt_header->bios_data_blocks_offset);
    uint16_t vbt_size = vbt_header->vbt_size;
    if (!bdb_->validate()
            || bdb_->bios_data_blocks_size > vbt_size
            || vbt_header->bios_data_blocks_offset + bdb_->bios_data_blocks_size > vbt_size) {
        LOG_ERROR("Failed to validate bdb header\n");
        return ZX_ERR_INTERNAL;
    }

    // TODO(stevensd): 196 seems old enough that all gen9 processors will have it. If we want to
    // support older hardware, we'll need to handle missing data.
    if (bdb_->version < 196) {
        LOG_ERROR("Out of date vbt (%d)\n", bdb_->version);
        return ZX_ERR_INTERNAL;
    }

    if (!ProcessDdiConfigs() || !CheckForLowVoltageEdp(pci)) {
        return ZX_ERR_INTERNAL;
    }

    ProcessBacklightData();

    return ZX_OK;
}

}
