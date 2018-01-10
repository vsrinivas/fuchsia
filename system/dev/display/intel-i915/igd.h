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
#include <zx/vmo.h>

#include "registers-ddi.h"

namespace i915 {
// Various definitions from IGD OpRegion/Software SCI documentation.

// Offsets into the PCI configuration space of IGD registers
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
    uint8_t unused2[12];
    // Specifies the DDI this config this corresponds to as well as type of DDI.
    uint8_t port_type;
    uint8_t unused3[21];
} ddi_config_t;

static_assert(offsetof(ddi_config_t, ddi_flags) == 2, "Bad ddi_flags offset");
static_assert(offsetof(ddi_config_t, port_type) == 16, "Bad port_type offset");
static_assert(sizeof(ddi_config_t) == 38, "Bad ddi_config_t size");

class IgdOpRegion {
public:
    IgdOpRegion();
    ~IgdOpRegion();
    zx_status_t Init(pci_protocol_t* pci);

    bool IsHdmi(registers::Ddi ddi) const {
        return ddi_type_[ddi] == kHdmi;
    }
    bool IsDvi(registers::Ddi ddi) const {
        return ddi_type_[ddi] == kDvi;
    }
    bool IsDp(registers::Ddi ddi) const {
        return ddi_type_[ddi] == kDp || ddi_type_[ddi] == kEdp;
    }
private:
    template<typename T> T* GetSection(uint16_t* size);
    uint8_t* GetSection(uint8_t tag, uint16_t* size);
    bool ProcessDdiConfigs();

    zx::vmo igd_opregion_pages_;
    uintptr_t igd_opregion_pages_base_;
    uintptr_t igd_opregion_pages_len_;
    igd_opregion_t* igd_opregion_;
    bios_data_blocks_header_t* bdb_;

    uint8_t ddi_type_[registers::kDdiCount];
    constexpr static uint8_t kNone = 0;
    constexpr static uint8_t kHdmi = 1;
    constexpr static uint8_t kDvi = 2;
    constexpr static uint8_t kDp = 3;
    constexpr static uint8_t kEdp = 4;
};

} // namespace i915
