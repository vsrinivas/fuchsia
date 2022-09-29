// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_IGD_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_IGD_H_

#include <inttypes.h>
#include <lib/device-protocol/pci.h>
#include <lib/zx/vmo.h>
#include <stddef.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <unordered_map>

#include <hwreg/bitfields.h>

#include "src/graphics/display/drivers/intel-i915-tgl/acpi-memory-region.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-ddi.h"

namespace i915_tgl {
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

  uint8_t major_version() const { return version >> 24; }
  uint8_t minor_version() const { return (version >> 16) & 0xff; }

  bool asle_supported() const { return supported_mailboxes & (1 << 2); }

  struct vbt_region_t {
    uint64_t rvda;
    uint32_t rvds;
  } __attribute__((__packed__));

  vbt_region_t vbt_region() {
    vbt_region_t region;
    // According to the IGD OpRegion spec v0.5, this offset is the beginning
    // of a reserved region.  It would be good to confirm this offset with a
    // newer version of the spec.
    std::memcpy(&region, &mailbox3[186], sizeof(vbt_region_t));
    return region;
  }

  bool validate() {
    const char* sig = "IntelGraphicsMem";
    return !memcmp(signature, reinterpret_cast<const void*>(sig), 16) &&
           kb_size >= (sizeof(struct igd_opregion) >> 10);
  }
} igd_opregion_t;

static_assert(sizeof(igd_opregion_t) == 0x2000, "Bad igd opregion len");
static_assert(offsetof(igd_opregion_t, mailbox4) == 1024, "Bad mailbox4 offset");

typedef struct sci_interface {
  uint32_t entry_and_exit_params;
  uint32_t additional_params;
  uint32_t driver_sleep_timeout;
  uint8_t rsvd[240];
} sci_interface_protocol_t;

static_assert(sizeof(sci_interface_protocol_t) == 252, "Bad sci_interface_protocol_t size");

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
    return !memcmp(signature, sig_prefix, 4) && sizeof(bios_data_blocks_header_t) < vbt_size &&
           bios_data_blocks_offset < vbt_size - sizeof(bios_data_blocks_header_t);
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

typedef struct __attribute__((packed)) ddi_config {
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

  uint8_t unused5[9];

  uint8_t type_c_config;
  DEF_SUBBIT(type_c_config, 0, is_usb_type_c);
  DEF_SUBBIT(type_c_config, 1, is_thunderbolt);

  uint8_t unused6[3];

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

  uint8_t unused[204];
  // Contains 16 nibbles, one for each panel type 0x0-0xf. If the value
  // is 0, then the panel is a low voltage panel.
  uint8_t vswing_preemphasis[8];
  // A bunch of other unused stuff
} edp_config_t;
static_assert(offsetof(edp_config_t, vswing_preemphasis) == 204, "Bad vswing_preemphasis offset");

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
  IgdOpRegion() = default;
  ~IgdOpRegion();
  zx_status_t Init(const ddk::Pci& pci);

  bool HasDdi(tgl_registers::Ddi ddi) const {
    return ddi_features_.find(ddi) != ddi_features_.end();
  }
  bool SupportsHdmi(tgl_registers::Ddi ddi) const {
    return HasDdi(ddi) && ddi_features_.at(ddi).supports_hdmi;
  }
  bool SupportsDvi(tgl_registers::Ddi ddi) const {
    return HasDdi(ddi) && ddi_features_.at(ddi).supports_dvi;
  }
  bool SupportsDp(tgl_registers::Ddi ddi) const {
    return HasDdi(ddi) && ddi_features_.at(ddi).supports_dp;
  }
  bool IsEdp(tgl_registers::Ddi ddi) const { return HasDdi(ddi) && ddi_features_.at(ddi).is_edp; }

  bool IsLowVoltageEdp(tgl_registers::Ddi ddi) const {
    // TODO(stevensd): Support the case where more than one type of edp panel is present.
    return HasDdi(ddi) && ddi_features_.at(ddi).is_edp && edp_is_low_voltage_;
  }

  uint8_t GetIBoost(tgl_registers::Ddi ddi, bool is_dp) const {
    return is_dp ? HasDdi(ddi) && ddi_features_.at(ddi).iboosts.dp_iboost
                 : HasDdi(ddi) && ddi_features_.at(ddi).iboosts.hdmi_iboost;
  }

  static constexpr uint8_t kUseDefaultIdx = 0xff;
  uint8_t GetHdmiBufferTranslationIndex(tgl_registers::Ddi ddi) const {
    ZX_DEBUG_ASSERT(SupportsHdmi(ddi) || SupportsDvi(ddi));
    return ddi_features_.at(ddi).hdmi_buffer_translation_idx;
  }

  double GetMinBacklightBrightness() const { return min_backlight_brightness_; }

  void SetIsEdpForTesting(tgl_registers::Ddi ddi, bool is_edp) {
    ddi_features_[ddi].is_edp = is_edp;
  }
  void SetSupportsDpForTesting(tgl_registers::Ddi ddi, bool value) {
    ddi_features_[ddi].supports_dp = value;
  }

 private:
  template <typename T>
  T* GetSection(uint16_t* size);
  uint8_t* GetSection(uint8_t tag, uint16_t* size);
  bool ProcessDdiConfigs();
  bool CheckForLowVoltageEdp(const ddk::Pci& pci);
  bool GetPanelType(const ddk::Pci& pci, uint8_t* type);
  bool Swsci(const ddk::Pci& pci, uint16_t function, uint16_t subfunction,
             uint32_t additional_param, uint16_t* exit_param, uint32_t* additional_res);
  void ProcessBacklightData();

  AcpiMemoryRegion memory_op_region_;

  // Empty if the VBT fits in the Memory OpRegion's Mailbox 4.
  AcpiMemoryRegion extended_vbt_region_;

  igd_opregion_t* igd_opregion_;
  bios_data_blocks_header_t* bdb_;

  struct DdiFeatures {
    bool supports_hdmi;
    bool supports_dvi;
    bool supports_dp;
    bool is_edp;
    bool is_type_c;
    bool is_thunderbolt;

    struct Iboost {
      uint8_t hdmi_iboost = 0;
      uint8_t dp_iboost = 0;
    };
    Iboost iboosts;
    uint8_t hdmi_buffer_translation_idx;
  };
  std::unordered_map<tgl_registers::Ddi, DdiFeatures> ddi_features_;

  bool edp_is_low_voltage_ = false;
  uint8_t panel_type_;
  double min_backlight_brightness_;
};

}  // namespace i915_tgl

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_IGD_H_
