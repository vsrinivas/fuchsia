// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <ddk/debug.h>
#include <hwreg/bitfields.h>
#include <zx/object.h>
#include <zx/vmar.h>

#include "igd.h"
#include "intel-i915.h"

namespace i915 {

IgdOpRegion::IgdOpRegion() {}

IgdOpRegion::~IgdOpRegion() {
    if (igd_opregion_pages_base_) {
        zx::vmar::root_self().unmap(igd_opregion_pages_base_, igd_opregion_pages_len_);
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
        zxlogf(ERROR, "i915: Couldn't find vbt general definitions\n");
        return false;
    }
    if (size < sizeof(general_definitions_t)) {
        zxlogf(ERROR, "i915: Bad size in vbt general definitions\n");
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
        uint8_t type;
        uint8_t idx;
        if (cfg->port_type < 4 || cfg->port_type == 12) {
            // Types 0, 1, 2, 3, and 12 are HDMI ports A, B, C, D, and E
            if (!ddi_flags.tmds()) {
                zxlogf(INFO, "i915: Malformed hdmi config\n");
                continue;
            }

            type = ddi_flags.not_hdmi() ? kDvi : kHdmi;
            idx = cfg->port_type < 4 ? static_cast<registers::Ddi>(cfg->port_type)
                                     : registers::DDI_E;
        } else if (7 <= cfg->port_type && cfg->port_type <= 11) {
            // Types 7, 8, 9, 10, 11 are DP ports B, C, D, A, E
            if (!ddi_flags.dp()) {
                zxlogf(INFO, "i915: Malformed dp config\n");
                continue;
            }

            type = ddi_flags.internal() ? kEdp : kDp;
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

        if (ddi_type_[idx] != kNone) {
            zxlogf(INFO, "i915: Duplicate ddi config\n");
            continue;
        }

        ddi_type_[idx] = type;
    }

    return true;
}

zx_status_t IgdOpRegion::Init(pci_protocol_t* pci) {
    uint32_t igd_addr;
    zx_status_t status = pci_config_read32(pci, kIgdOpRegionAddrReg, &igd_addr);
    if (status != ZX_OK || !igd_addr) {
        zxlogf(ERROR, "i915: Failed to locate IGD OpRegion (%d)\n", status);
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
        zxlogf(ERROR, "i915: Failed to access IGD OpRegion (%d)\n", status);
        return status;
    }
    igd_opregion_pages_ = zx::vmo(vmo);

    status = zx::vmar::root_self().map(0, igd_opregion_pages_, 0, igd_opregion_pages_len_,
                                       ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                                       &igd_opregion_pages_base_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "i915: Failed to map IGD OpRegion (%d)\n", status);
        return status;
    }

    igd_opregion_ = reinterpret_cast<igd_opregion_t*>(
            igd_opregion_pages_base_ + (igd_addr % PAGE_SIZE));
    if (!igd_opregion_->validate()) {
        zxlogf(ERROR, "i915: Failed to validate IGD OpRegion\n");
        return ZX_ERR_INTERNAL;
    }

    vbt_header_t* vbt_header = reinterpret_cast<vbt_header_t*>(&igd_opregion_->mailbox4);
    if (!vbt_header->validate()) {
        zxlogf(ERROR, "i915: Failed to validate vbt header\n");
        return ZX_ERR_INTERNAL;
    }

    bdb_ = reinterpret_cast<bios_data_blocks_header_t*>(
            igd_opregion_->mailbox4 + vbt_header->bios_data_blocks_offset);
    uint16_t vbt_size = vbt_header->vbt_size;
    if (!bdb_->validate()
            || bdb_->bios_data_blocks_size > vbt_size
            || vbt_header->bios_data_blocks_offset + bdb_->bios_data_blocks_size > vbt_size) {
        zxlogf(ERROR, "i915: Failed to validate bdb header\n");
        return ZX_ERR_INTERNAL;
    }

    // TODO(stevensd): 196 seems old enough that all gen9 processors will have it. If we want to
    // support older hardware, we'll need to handle missing data.
    if (bdb_->version < 196) {
        zxlogf(ERROR, "i915: Out of date vbt (%d)\n", bdb_->version);
        return ZX_ERR_INTERNAL;
    }

    return ProcessDdiConfigs() ? ZX_OK : ZX_ERR_INTERNAL;
}

}
