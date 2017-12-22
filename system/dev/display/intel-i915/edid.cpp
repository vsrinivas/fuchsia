// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>

#include <stddef.h>
#include <string.h>
#include <zircon/assert.h>

#include "edid.h"

namespace {

template<typename T> bool base_validate(T* block) {
    static_assert(sizeof(T) == edid::kBlockSize, "Size check for Edid struct");

    uint8_t* edid_bytes = reinterpret_cast<uint8_t*>(block);
    if (edid_bytes[0] != T::kTag) {
        return false;
    }

    // The last byte of the 128-byte EDID data is a checksum byte which
    // should make the 128 bytes sum to zero.
    uint8_t sum = 0;
    for (uint32_t i = 0; i < edid::kBlockSize; ++i) {
        sum = static_cast<uint8_t>(sum + edid_bytes[i]);
    }
    return sum == 0;
}

} // namespace

namespace edid {

bool BaseEdid::validate() {
    static const uint8_t kEdidHeader[8] = {0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0};
    return base_validate<BaseEdid>(this) && memcmp(header, kEdidHeader, sizeof(kEdidHeader)) == 0;
}

bool BlockMap::validate() {
    return base_validate<BlockMap>(this);
}

bool CeaEdidTimingExtension::validate() {
    return base_validate<CeaEdidTimingExtension>(this);
}

Edid::Edid(EdidSource* edid_source) : edid_source_(edid_source) { }

bool Edid::Init() {
    // Initialize the base edid data
    return ReadBlock(0, &base_edid_);
}

template<typename T> bool Edid::ReadBlock(uint8_t block_num, T* block) {
    uint8_t segment = block_num / 2;
    uint8_t segment_offset = block_num % 2 ? 128 : 0;
    if (!edid_source_->ReadEdid(segment, segment_offset,
                                reinterpret_cast<uint8_t*>(block), kBlockSize)) {
        zxlogf(TRACE, "Failed to read block %d\n", block_num);
        return false;
    } else if (!block->validate()) {
        zxlogf(TRACE, "Failed to validate block %d\n", block_num);
        return false;
    }
    return true;
}

bool Edid::CheckBlockMap(uint8_t block_num, bool* is_hdmi) {
    BlockMap map;
    if (!ReadBlock(block_num, &map)) {
        return false;
    }
    for (uint8_t i = 0; i < fbl::count_of(map.tag_map); i++) {
        if (map.tag_map[i] == CeaEdidTimingExtension::kTag) {
            if (!CheckBlockForHdmiVendorData(static_cast<uint8_t>(i + block_num), is_hdmi)) {
                return false;
            } else if (*is_hdmi) {
                return true;
            }
        }
    }
    return true;
}

bool Edid::CheckBlockForHdmiVendorData(uint8_t block_num, bool* is_hdmi) {
    CeaEdidTimingExtension block;
    if (!ReadBlock(block_num, &block)) {
        return false;
    }
    if (block.revision_number < 0x03) {
        zxlogf(TRACE, "Skipping block revision %d %d\n", block_num, block.revision_number);
        return true;
    }
    // dtd_start_idx == 0 means no detailed timing descriptors AND no data block collection.
    if (block.dtd_start_idx == 0) {
        return true;
    }
    // dtd_start_idx must be within (or immediately after) payload. If not, abort
    // because we have a malformed edid.
    uint8_t payload_offset = offsetof(CeaEdidTimingExtension, payload);
    if (!(payload_offset <= block.dtd_start_idx
            && block.dtd_start_idx <= (payload_offset + fbl::count_of(block.payload)))) {
        return false;
    }
    uint32_t idx = 0;
    size_t end = block.dtd_start_idx - offsetof(CeaEdidTimingExtension, payload);
    while (idx < end) {
        DataBlock* data_block = reinterpret_cast<DataBlock*>(block.payload + idx);
        // Compute the start of the next data block, and use that to ensure that the current
        // block doesn't run past the end of the data block collection.
        idx = idx + 1 + data_block->length();
        if (idx > end) {
            return false;
        }
        if (data_block->type() == VendorSpecificBlock::kType) {
            // HDMI's 24-bit IEEE registration is 0x000c03 - vendor_number is little endian
            if (data_block->payload.vendor.vendor_number[0] == 0x03
                    && data_block->payload.vendor.vendor_number[1] == 0x0c
                    && data_block->payload.vendor.vendor_number[2] == 0x00) {
                *is_hdmi = true;
                return true;
            }
        }
    }

    return true;
}

bool Edid::CheckForHdmi(bool* is_hdmi) {
    ZX_DEBUG_ASSERT(base_edid_.validate());

    *is_hdmi = false;
    if (base_edid_.num_extensions == 0) {
        return true;
    } else if (base_edid_.num_extensions == 1) {
        // There's only one extension to check
        return CheckBlockForHdmiVendorData(1, is_hdmi);
    } else {
        return CheckBlockMap(1, is_hdmi)
            && (*is_hdmi || base_edid_.num_extensions < 128 || CheckBlockMap(128, is_hdmi));
    }
}

} // namespace edid
