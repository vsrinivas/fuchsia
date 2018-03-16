// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/pci.h>

#include <hwreg/bitfields.h>
#include <inttypes.h>
#include <stddef.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/types.h>
#include <lib/zx/vmo.h>

#include "registers-ddi.h"

namespace i915 {
// Various definitions from IGD OpRegion/Software SCI documentation.

// Offsets into the PCI configuration space of IGD registers
constexpr uint16_t kIgdSwSciReg = 0xe8;
constexpr uint16_t kIgdOpRegionAddrReg = 0xfc;

// Length of the igd opregion
constexpr uint32_t kIgdOpRegionLen = 0x2000;

constexpr uint32_t kMaxVbtSize = 6144;

typedef struct igd_opregion {
    uint8_t signature[16];
    uint32_t kb_size;
    uint32_t version;
    uint8_t system_bios_build_version[32];
    uint8_t video_bios_build_version[16];
    uint8_t graphics_bios_build_version[16];
    uint32_t supported_mailboxes;
    uint32_t driver_model;
    uint32_t pcon;
    uint8_t gop_version[32];
    uint8_t rsvd[124];

    uint8_t mailbox1[256];
    uint8_t mailbox2[256];
    uint8_t mailbox3[256];
    uint8_t mailbox4[kMaxVbtSize];
    uint8_t mailbox5[1024];

    bool validate() {
        const char* sig = "IntelGraphicsMem";
        return !memcmp(signature, reinterpret_cast<const void*>(sig), 16)
                && kb_size >= (sizeof(struct igd_opregion) >> 10);
    }
} igd_opregion_t;

static_assert(sizeof(igd_opregion_t) == 0x2000, "Bad igd opregion len");
static_assert(offsetof(igd_opregion_t, mailbox4) == 1024, "Bad mailbox4 offset");

typedef struct sci_interface {
    uint32_t entry_and_exit_params;
    uint32_t additional_params;
    uint32_t driver_sleep_timeout;
    uint8_t rsvd[240];
} sci_interface_t;

static_assert(sizeof(sci_interface_t) == 252, "Bad sci_interface_t size");

// Header for each bios data block.
typedef struct block_header {
    uint8_t type;
    // Size of the block, not including the header
    uint8_t size_low;
    uint8_t size_high;
} block_header_t;
static_assert(sizeof(block_header_t) == 3, "Bad block_header size");

typedef struct bios_data_blocks_header {
    uint8_t signature[16];
    uint16_t version;
    // Size of the header by itself
    uint16_t header_size;
    // Size of the header + all the blocks
    uint16_t bios_data_blocks_size;

    bool validate() {
        const char* sig = "BIOS_DATA_BLOCK";
        return !memcmp(signature, sig, 15) && bios_data_blocks_size >= sizeof(block_header_t);
    }
} bios_data_blocks_header_t;
static_assert(sizeof(bios_data_blocks_header_t) == 22, "Bad bios_data_blocks_header size");

typedef struct vbt_header {
    uint8_t signature[20];
    uint16_t version;
    uint16_t header_size;
    uint16_t vbt_size;
    uint8_t checksum;
    uint8_t rsvd;
    uint32_t bios_data_blocks_offset;
    uint32_t aim_offset[4];

    bool validate() {
        const char* sig_prefix = "$VBT";
        return !memcmp(signature, sig_prefix, 4)
            && sizeof(bios_data_blocks_header_t) < vbt_size && vbt_size <= kMaxVbtSize
            && bios_data_blocks_offset < vbt_size - sizeof(bios_data_blocks_header_t);
    }
} vbt_header_t;
static_assert(sizeof(vbt_header_t) == 48, "Bad vbt_header size");

typedef struct general_definitions {
    static constexpr uint32_t kBlockType = 2;

    uint8_t unused[4];
    // Contains the length of each entry in ddis.
    uint8_t ddi_config_size;
    // Array of ddi_config structures.
    uint8_t ddis[0];
} general_definitions_t;

// Bitfield for ddi_config's ddi_flags register
class DdiFlags : public hwreg::RegisterBase<DdiFlags, uint16_t> {
public:
    DEF_BIT(12, internal);
    DEF_BIT(11, not_hdmi);
    DEF_BIT(4, tmds);
    DEF_BIT(2, dp);

    static auto Get() { return hwreg::RegisterAddr<DdiFlags>(0); }
};

typedef struct ddi_config {
    uint8_t unused1[2];
    // See DdiFlags class
    uint16_t ddi_flags;
    uint8_t unused2[3];

    uint8_t hdmi_cfg;
    // Index into the recommended buffer translation table to use when
    // configuring DDI_BUF_TRANS[9] for HDMI/DVI.
    DEF_SUBFIELD(hdmi_cfg, 3, 0, ddi_buf_trans_idx);

    uint8_t unused3[8];
    // Specifies the DDI this config this corresponds to as well as type of DDI.
    uint8_t port_type;
    uint8_t unused4[6];

    uint8_t flags;
    // Flag that indicates that there is an iboost override. An override enables
    // iboost for all DDI_BUF_TRANS values and overrides the recommended iboost.
    DEF_SUBBIT(flags, 3, has_iboost_override);

    uint8_t unused5[13];

    uint8_t iboost_levels;
    // The iboost override level, if has_iboost_override is set.
    DEF_SUBFIELD(iboost_levels, 7, 4, hdmi_iboost_override);
    DEF_SUBFIELD(iboost_levels, 3, 0, dp_iboost_override);
} ddi_config_t;
static_assert(offsetof(ddi_config_t, ddi_flags) == 2, "Bad ddi_flags offset");
static_assert(offsetof(ddi_config_t, hdmi_cfg) == 7, "Bad hdmi_cfg offset");
static_assert(offsetof(ddi_config_t, port_type) == 16, "Bad port_type offset");
static_assert(offsetof(ddi_config_t, flags) == 23, "Bad flags offset");
static_assert(offsetof(ddi_config_t, iboost_levels) == 37, "Bad iboost_levels offset");
static_assert(sizeof(ddi_config_t) == 38, "Bad ddi_config_t size");

typedef struct edp_config {
    static constexpr uint32_t kBlockType = 27;

    uint8_t unused[188];
    // Contains 16 nibbles, one for each panel type 0x0-0xf. If the value
    // is 0, then the panel is a low voltage panel.
    uint8_t vswing_preemphasis[8];
    // A bunch of other unused stuff
} edp_config_t;
static_assert(offsetof(edp_config_t, vswing_preemphasis) == 188, "Bad vswing_preemphasis offset");

typedef struct lvds_config {
    static constexpr uint32_t kBlockType = 40;

    // The default panel type for the hardware. Can be overridden by the IGD
    // SCI panel details function.
    uint8_t panel_type;
    // A bunch of other unused stuff
} lvds_config_t;

typedef struct lfp_backlight_entry {
    uint8_t flags;
    uint8_t pwm_freq_hz_low;
    uint8_t pwm_freq_hz_high;
    uint8_t min_brightness;
    uint8_t unused[2];
} lfp_backlight_entry_t;
static_assert(sizeof(lfp_backlight_entry_t) == 6, "Bad struct size");

typedef struct lfp_backlight {
    static constexpr uint32_t kBlockType = 43;

    uint8_t entry_size;
    lfp_backlight_entry_t entries[16];
    uint8_t level[16];
} lfp_backlight_t;
static_assert(sizeof(lfp_backlight_t) == 113, "Bad struct size");

class IgdOpRegion {
public:
    IgdOpRegion();
    ~IgdOpRegion();
    zx_status_t Init(pci_protocol_t* pci);

    bool SupportsHdmi(registers::Ddi ddi) const {
        return ddi_supports_hdmi_[ddi];
    }
    bool SupportsDvi(registers::Ddi ddi) const {
        return ddi_supports_dvi_[ddi];
    }
    bool SupportsDp(registers::Ddi ddi) const {
        return ddi_supports_dp_[ddi];
    }
    bool IsEdp(registers::Ddi ddi) const {
        return ddi_is_edp_[ddi];
    }

    bool IsLowVoltageEdp(registers::Ddi ddi) const {
        ZX_DEBUG_ASSERT(SupportsDp(ddi));
        // TODO(stevensd): Support the case where more than one type of edp panel is present.
        return ddi_is_edp_[ddi] && edp_is_low_voltage_;
    }

    uint8_t GetIBoost(registers::Ddi ddi, bool is_dp) const {
        return is_dp ? iboosts_[ddi].dp_iboost : iboosts_[ddi].hdmi_iboost;
    }

    static constexpr uint8_t kUseDefaultIdx = 0xff;
    uint8_t GetHdmiBufferTranslationIndex(registers::Ddi ddi) const {
        ZX_DEBUG_ASSERT(SupportsHdmi(ddi) || SupportsDvi(ddi));
        return hdmi_buffer_translation_idx_[ddi];
    }

    double GetMinBacklightBrightness() const {
        return min_backlight_brightness_;
    }

private:
    template<typename T> T* GetSection(uint16_t* size);
    uint8_t* GetSection(uint8_t tag, uint16_t* size);
    bool ProcessDdiConfigs();
    bool CheckForLowVoltageEdp(pci_protocol_t* pci);
    bool GetPanelType(pci_protocol_t* pci, uint8_t* type);
    bool Swsci(pci_protocol_t* pci, uint16_t function, uint16_t subfunction,
               uint32_t additional_param, uint16_t* exit_param, uint32_t* additional_res);
    void ProcessBacklightData();

    zx::vmo igd_opregion_pages_;
    uintptr_t igd_opregion_pages_base_;
    uintptr_t igd_opregion_pages_len_;
    igd_opregion_t* igd_opregion_;
    bios_data_blocks_header_t* bdb_;

    bool ddi_supports_hdmi_[registers::kDdiCount];
    bool ddi_supports_dvi_[registers::kDdiCount];
    bool ddi_supports_dp_[registers::kDdiCount];
    bool ddi_is_edp_[registers::kDdiCount];

    bool edp_is_low_voltage_;
    uint8_t panel_type_;
    double min_backlight_brightness_;

    struct {
        uint8_t hdmi_iboost;
        uint8_t dp_iboost;
    } iboosts_[registers::kDdiCount];
    uint8_t hdmi_buffer_translation_idx_[registers::kDdiCount];
};

} // namespace i915
