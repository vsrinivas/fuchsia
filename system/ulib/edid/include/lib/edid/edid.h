// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/limits.h>
#include <fbl/unique_ptr.h>
#include <hwreg/bitfields.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace edid {

// The size of an EDID block;
static constexpr uint32_t kBlockSize = 128;

// Definitions for parsing EDID data.

// EDID 18-byte detailed timing descriptor.
//
// Many of the parameters in the timing descriptor are split across
// multiple fields, so we define various accessors for reading them.
//
// See "Table 3.21 - Detailed Timing Definition - Part 1" (in Release
// A, Revision 2 of the EDID spec, 2006).
struct DetailedTimingDescriptor {
    uint32_t horizontal_addressable() const {
        return horizontal_addressable_low | (horizontal_addressable_high() << 8);
    }
    uint32_t horizontal_blanking() const {
        return horizontal_blanking_low | (horizontal_blanking_high() << 8);
    }
    uint32_t vertical_addressable() const {
        return vertical_addressable_low | (vertical_addressable_high() << 8);
    }
    uint32_t vertical_blanking() const {
        return vertical_blanking_low | (vertical_blanking_high() << 8);
    }
    uint32_t horizontal_front_porch() const {
        return horizontal_front_porch_low | (horizontal_front_porch_high() << 8);
    }
    uint32_t horizontal_sync_pulse_width() const {
        return horizontal_sync_pulse_width_low | (horizontal_sync_pulse_width_high() << 8);
    }
    uint32_t vertical_front_porch() const {
        return vertical_front_porch_low() | (vertical_front_porch_high() << 4);
    }
    uint32_t vertical_sync_pulse_width() const {
        return vertical_sync_pulse_width_low() |
               (vertical_sync_pulse_width_high() << 4);
    }

    // Offset 0
    uint16_t pixel_clock_10khz;
    // Offset 2
    uint8_t horizontal_addressable_low;
    uint8_t horizontal_blanking_low;
    uint8_t horizontal_fields1;
    DEF_SUBFIELD(horizontal_fields1, 7, 4, horizontal_addressable_high);
    DEF_SUBFIELD(horizontal_fields1, 3, 0, horizontal_blanking_high);
    // Offset 5
    uint8_t vertical_addressable_low;
    uint8_t vertical_blanking_low;
    uint8_t vertical_fields1;
    DEF_SUBFIELD(vertical_fields1, 7, 4, vertical_addressable_high);
    DEF_SUBFIELD(vertical_fields1, 3, 0, vertical_blanking_high);
    // Offset 8
    uint8_t horizontal_front_porch_low;
    uint8_t horizontal_sync_pulse_width_low;
    // Offset 10
    uint8_t vertical_fields2;
    DEF_SUBFIELD(vertical_fields2, 7, 4, vertical_front_porch_low);
    DEF_SUBFIELD(vertical_fields2, 3, 0, vertical_sync_pulse_width_low);
    // Offset 11
    uint8_t combined;
    DEF_SUBFIELD(combined, 7, 6, horizontal_front_porch_high);
    DEF_SUBFIELD(combined, 5, 4, horizontal_sync_pulse_width_high);
    DEF_SUBFIELD(combined, 3, 2, vertical_front_porch_high);
    DEF_SUBFIELD(combined, 1, 0, vertical_sync_pulse_width_high);
    uint8_t rest[5]; // Fields that we don't need to read yet.
    uint8_t features;
    DEF_SUBBIT(features, 7, interlaced);
    DEF_SUBFIELD(features, 4, 3, type);
    DEF_SUBBIT(features, 2, vsync_polarity);
    DEF_SUBBIT(features, 1, hsync_polarity);
};
#define TYPE_ANALOG 0
#define TYPE_ANALOG_BIPOLAR 1
#define TYPE_DIGITAL_COMPOSITE 2
#define TYPE_DIGITAL_SEPARATE 3

static_assert(sizeof(DetailedTimingDescriptor) == 18, "Size check for EdidTimingDesc");

struct StandardTimingDescriptor {
    uint32_t horizontal_resolution() const { return (byte1 + 31) * 8; }
    uint32_t vertical_resolution(uint8_t edid_version, uint8_t edid_revision) const {
        if (aspect_ratio() == 0) {
            if (edid_version < 1 || (edid_version == 1 && edid_version < 3)) {
                return horizontal_resolution();
            } else {
                return horizontal_resolution() * 10 / 16;
            }
        } else if (aspect_ratio() == 1) {
            return horizontal_resolution() * 3 / 4;
        } else if (aspect_ratio() == 2) {
            return horizontal_resolution() * 4 / 5;
        } else if (aspect_ratio() == 3) {
            return horizontal_resolution() * 9 / 16;
        } else {
            ZX_DEBUG_ASSERT(false);
            return 0;
        }
    }

    uint8_t byte1;
    uint8_t byte2;
    DEF_SUBFIELD(byte2, 7, 6, aspect_ratio);
    DEF_SUBFIELD(byte2, 5, 0, vertical_freq);
};

// This covers the "base" EDID data -- the first 128 bytes (block 0).  In
// many cases, that is all the display provides, but there may be more data
// in extension blocks.
//
// See "Table 3.1 - EDID Structure Version 1, Revision 4" (in Release
// A, Revision 2 of the EDID spec, 2006).
struct BaseEdid {
    bool validate() const;
    // Not actually a tag, but the first byte will always be this
    static constexpr uint8_t kTag = 0x00;

    // Offset 0
    uint8_t header[8];
    uint8_t unused1[10];
    uint8_t edid_version;
    uint8_t edid_revision;
    uint8_t video_input_definition;
    DEF_SUBBIT(video_input_definition, 7, digital);

    uint8_t various[17]; // Fields that we don't need to read yet.
    StandardTimingDescriptor standard_timings[8];
    DetailedTimingDescriptor detailed_timings[4];
    uint8_t num_extensions;
    uint8_t checksum_byte;
};

static_assert(offsetof(BaseEdid, edid_version) == 0x12, "Layout check");
static_assert(offsetof(BaseEdid, standard_timings) == 0x26, "Layout check");
static_assert(offsetof(BaseEdid, detailed_timings) == 0x36, "Layout check");

// EDID block type map. Block 1 if there are >1 blocks, and block
// 128 if there are >128 blocks. See EDID specification for the meaning
// of each entry in the tag_map
struct BlockMap {
    static constexpr uint8_t kTag = 0xf0;
    bool validate() const;

    uint8_t tag;
    uint8_t tag_map[126];
    uint8_t checksum_byte;
};

// Version 3 of the CEA EDID Timing Extension
struct CeaEdidTimingExtension {
    static constexpr uint8_t kTag = 0x02;
    bool validate() const;

    uint8_t tag;
    uint8_t revision_number;
    uint8_t dtd_start_idx;

    uint8_t combined;
    DEF_SUBBIT(combined, 7, underscan);
    DEF_SUBBIT(combined, 6, basic_audio);
    DEF_SUBBIT(combined, 5, ycbcr_444);
    DEF_SUBBIT(combined, 4, ycbcr_422);
    DEF_SUBFIELD(combined, 3, 0, native_format_dtds);

    uint8_t payload[123];
    uint8_t checksum_byte;
};

// Short audio descriptor from CEA EDID timing extension's data block collection.
struct ShortAudioDescriptor {
    static constexpr uint8_t kType = 1;

    uint8_t format_and_channels;
    DEF_SUBFIELD(format_and_channels, 6, 3, format);
    DEF_SUBFIELD(format_and_channels, 2, 0, num_channels_minus_1);
    uint8_t sampling_frequencies;
    uint8_t bitrate;
};
static_assert(sizeof(ShortAudioDescriptor) == 3, "Bad size for ShortAudioDescriptor");

// Short video descriptor from CEA EDID timing extension's data block collection.
struct ShortVideoDescriptor {
    static constexpr uint8_t kType = 2;

    uint8_t data;
    DEF_SUBBIT(data, 7, native);
    DEF_SUBFIELD(data, 6, 0, standard_mode_idx);
};
static_assert(sizeof(ShortVideoDescriptor) == 1, "Bad size for ShortVideoDescriptor");

// Vendor specific block from CEA EDID timing extension's data block collection.
struct VendorSpecificBlock {
    static constexpr uint8_t kType = 3;

    uint8_t vendor_number[3];
    uint8_t physical_addr_low;
    uint8_t physical_addr_high;
    // The payload contains vendor defined data. It is only valid up to the
    // index specified by the data block's length.
    uint8_t payload[26];
};
static_assert(sizeof(VendorSpecificBlock) == 31, "Bad size for VendorSpecificBlock");

// Short speaker descriptor from CEA EDID timing extension's data block collection.
struct ShortSpeakerDescriptor {
    static constexpr uint8_t kType = 4;

    uint8_t features;
    DEF_SUBBIT(features, 6, rear_left_right_center);
    DEF_SUBBIT(features, 5, front_left_right_center);
    DEF_SUBBIT(features, 4, rear_center);
    DEF_SUBBIT(features, 3, rear_left_right);
    DEF_SUBBIT(features, 2, front_center);
    DEF_SUBBIT(features, 1, lfe);
    DEF_SUBBIT(features, 0, front_left_right);
    uint8_t reserved;
    uint8_t reserved2;
};
static_assert(sizeof(ShortSpeakerDescriptor) == 3, "Bad size for ShortSpeakerDescriptor");

// Data block from CEA EDID timing extension's data block collection. Although this
// struct is 32 bytes long, only the first length+1 bytes are actually valid.
struct DataBlock {
    uint8_t header;
    DEF_SUBFIELD(header, 7, 5, type);
    DEF_SUBFIELD(header, 4, 0, length);

    union {
        ShortAudioDescriptor audio;
        // Only valid up to the index specified by length;
        ShortVideoDescriptor video[31];
        VendorSpecificBlock vendor;
        ShortSpeakerDescriptor speaker;
    } payload;
};
static_assert(sizeof(DataBlock) == 32, "Bad size for DataBlock");

class EdidDdcSource {
public:
    // The I2C address for writing the DDC segment
    static constexpr int kDdcSegmentI2cAddress = 0x30;
    // The I2C address for writing the DDC data offset/reading DDC data
    static constexpr int kDdcDataI2cAddress = 0x50;

    virtual bool DdcRead(uint8_t segment, uint8_t offset, uint8_t* buf, uint8_t len) = 0;
};

typedef struct timing_params {
    uint32_t pixel_freq_10khz;

    uint32_t horizontal_addressable;
    uint32_t horizontal_front_porch;
    uint32_t horizontal_sync_pulse;
    uint32_t horizontal_blanking;

    uint32_t vertical_addressable;
    uint32_t vertical_front_porch;
    uint32_t vertical_sync_pulse;
    uint32_t vertical_blanking;

    uint32_t vertical_sync_polarity;
    uint32_t horizontal_sync_polarity;
    uint32_t interlaced;
} timing_params_t;

class Edid {
public:
    class timing_iterator {
    public:
        timing_iterator() { }

        timing_iterator& operator++();

        const timing_params& operator*() {
            return params_;
        }

        const timing_params* operator->() const {
            return &params_;
        }

        bool operator!=(const timing_iterator& rhs) const {
            return !(edid_ == rhs.edid_
                    && block_idx_ == rhs.block_idx_
                    && timing_idx_ == rhs.timing_idx_);
        }

    private:
        friend Edid;
        explicit timing_iterator(const Edid* edid, uint8_t block_idx, uint32_t timing_idx)
                : edid_(edid), block_idx_(block_idx), timing_idx_(timing_idx) {
            ++(*this);
        }
        void Advance();

        timing_params params_;

        const Edid* edid_ = nullptr;
        // The block index in which we're looking for DTDs. If it's num_blocks+1, then
        // we're looking at standard timings. If it's UINT8_MAX, then we're at the end.
        uint8_t block_idx_ = UINT8_MAX;
        uint32_t timing_idx_ = UINT32_MAX;
    };


    // Creates an Edid from the EdidDdcSource. Does not retain a reference to the source.
    bool Init(EdidDdcSource* edid_source, const char** err_msg);
    // Creates an Edid from raw bytes. The bytes array must remain valid for the duration
    // of the Edid object's lifetime.
    bool Init(const uint8_t* bytes, uint16_t len, const char** err_msg);

    bool CheckForHdmi(bool* is_hdmi) const;

    void Print(void (*print_fn)(const char* str)) const;

    const uint8_t* edid_bytes() const { return bytes_; }
    uint16_t edid_length() const { return len_; }

    timing_iterator begin() const { return timing_iterator(this, 0, UINT32_MAX); }
    timing_iterator end() const { return timing_iterator(this, UINT8_MAX, UINT32_MAX); }

private:
    bool CheckBlockMap(uint8_t block_num, bool* is_hdmi) const;
    bool CheckBlockForHdmiVendorData(uint8_t block_num, bool* is_hdmi) const;
    template<typename T> bool GetBlock(uint8_t block_num, T* block) const;

    // TODO(stevensd): make this a pointer that refers directly to edid_bytes_
    BaseEdid base_edid_;

    fbl::unique_ptr<uint8_t[]> edid_bytes_;

    const uint8_t* bytes_;
    uint16_t len_;
};

} // namespace edid
