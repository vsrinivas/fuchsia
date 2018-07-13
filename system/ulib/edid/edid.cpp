// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/edid/edid.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <zircon/assert.h>

namespace {

template<typename T> bool base_validate(const T* block) {
    static_assert(sizeof(T) == edid::kBlockSize, "Size check for Edid struct");

    const uint8_t* edid_bytes = reinterpret_cast<const uint8_t*>(block);
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

uint32_t round_div(double num, double div) {
    return (uint32_t) ((num / div) + .5);
}

} // namespace

namespace edid {

bool BaseEdid::validate() const {
    static const uint8_t kEdidHeader[8] = {0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0};
    return base_validate<BaseEdid>(this) && memcmp(header, kEdidHeader, sizeof(kEdidHeader)) == 0;
}

bool BlockMap::validate() const {
    return base_validate<BlockMap>(this);
}

bool CeaEdidTimingExtension::validate() const {
    if (!(dtd_start_idx <= sizeof(payload) && base_validate<CeaEdidTimingExtension>(this))) {
        return false;
    }
    size_t offset = 0;
    size_t dbc_end = dtd_start_idx - offsetof(CeaEdidTimingExtension, payload);
    while (offset < dbc_end) {
        const DataBlock* data_block = reinterpret_cast<const DataBlock*>(payload + offset);
        offset += (1 + data_block->length()); // Length doesn't include the header
        // Check that the block doesn't run past the end if the dbc
        if (offset > dbc_end) {
            return false;
        }
    }
    return true;
}

bool Edid::Init(EdidDdcSource* edid_source, const char** err_msg) {
    BaseEdid base_edid;
    if (!edid_source->DdcRead(0, 0, reinterpret_cast<uint8_t*>(&base_edid), kBlockSize)) {
        *err_msg = "Failed to read base edid";
        return false;
    } else if (!base_edid.validate()) {
        *err_msg = "Failed to validate base edid";
        return false;
    }

    uint16_t edid_length = static_cast<uint16_t>((base_edid.num_extensions + 1) * kBlockSize);
    fbl::AllocChecker ac;
    edid_bytes_ = fbl::unique_ptr<uint8_t[]>(new (&ac) uint8_t[edid_length]);
    if (!ac.check()) {
        *err_msg = "Failed to allocate edid storage";
        return false;
    }

    memcpy(edid_bytes_.get(), reinterpret_cast<void*>(&base_edid), kBlockSize);
    for (uint8_t i = 1; i && i <= base_edid.num_extensions; i++) {
        uint8_t segment = i / 2;
        uint8_t segment_offset = i % 2 ? kBlockSize : 0;
        if (!edid_source->DdcRead(segment, segment_offset,
                                  edid_bytes_.get() + i * kBlockSize, kBlockSize)) {
            *err_msg = "Failed to read full edid";
            return false;
        }
    }

    return Init(edid_bytes_.get(), edid_length, err_msg);
}

bool Edid::Init(const uint8_t* bytes, uint16_t len, const char** err_msg) {
    // The maximum size of an edid is 255 * 128 bytes, so any 16 bit multiple is fine.
    if (len == 0 || len % kBlockSize != 0) {
        *err_msg = "Invalid edid length";
        return false;
    }
    bytes_ = bytes;
    len_ = len;
    if (!GetBlock(0, &base_edid_)) {
        *err_msg = "Failed to find base edid";
        return false;
    }
    if (((base_edid_.num_extensions + 1) * kBlockSize) != len) {
        *err_msg = "Bad extension count";
        return false;
    }
    if (!base_edid_.digital()) {
        *err_msg = "Analog displays not supported";
        return false;
    }
    // TODO(stevensd): validate all of the extensions
    return true;
}

template<typename T> bool Edid::GetBlock(uint8_t block_num, T* block) const {
    if (block_num * kBlockSize > len_) {
        return false;
    }
    memcpy(reinterpret_cast<void*>(block), bytes_ + block_num * kBlockSize, kBlockSize);
    if (!block->validate()) {
        return false;
    }
    return true;
}

bool Edid::CheckBlockMap(uint8_t block_num, bool* is_hdmi) const {
    BlockMap map;
    if (!GetBlock(block_num, &map)) {
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

bool Edid::CheckBlockForHdmiVendorData(uint8_t block_num, bool* is_hdmi) const {
    CeaEdidTimingExtension block;
    if (!GetBlock(block_num, &block)) {
        return false;
    }
    if (block.revision_number < 0x03) {
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
        const DataBlock* data_block = reinterpret_cast<const DataBlock*>(block.payload + idx);
        if (data_block->type() == VendorSpecificBlock::kType) {
            // HDMI's 24-bit IEEE registration is 0x000c03 - vendor_number is little endian
            if (data_block->payload.vendor.vendor_number[0] == 0x03
                    && data_block->payload.vendor.vendor_number[1] == 0x0c
                    && data_block->payload.vendor.vendor_number[2] == 0x00) {
                *is_hdmi = true;
                return true;
            }
        }
        idx = idx + 1 + data_block->length();
    }

    return true;
}

bool Edid::CheckForHdmi(bool* is_hdmi) const {
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

void convert_dtd_to_timing(const DetailedTimingDescriptor& dtd, timing_params* params) {
    params->pixel_freq_10khz = dtd.pixel_clock_10khz;
    params->horizontal_addressable = dtd.horizontal_addressable();
    params->horizontal_front_porch = dtd.horizontal_front_porch();
    params->horizontal_sync_pulse = dtd.horizontal_sync_pulse_width();
    params->horizontal_blanking = dtd.horizontal_blanking();

    params->vertical_addressable = dtd.vertical_addressable();
    params->vertical_front_porch = dtd.vertical_front_porch();
    params->vertical_sync_pulse = dtd.vertical_sync_pulse_width();
    params->vertical_blanking = dtd.vertical_blanking();

    if (dtd.type() != TYPE_DIGITAL_SEPARATE) {
        printf("edid: Ignoring bad timing type %d\n", dtd.type());
    }
    params->flags = (dtd.vsync_polarity() ? timing_params::kPositiveVsync : 0)
            | (dtd.hsync_polarity() ? timing_params::kPositiveHsync : 0)
            | (dtd.interlaced() ? timing_params::kInterlaced : 0);

    double total_pxls =
            (params->horizontal_addressable + params->horizontal_blanking) *
            (params->vertical_addressable + params->vertical_blanking);
    double pixel_clock_hz = params->pixel_freq_10khz * 1000 * 10;
    params->vertical_refresh_e2 =
            static_cast<uint32_t>(round(100 * pixel_clock_hz / total_pxls));
}

void convert_std_to_timing(const BaseEdid& edid,
                           const StandardTimingDescriptor& std, timing_params* params) {
    // Pick the largest resolution advertised by the display and then use the
    // generalized timing formula to compute the timing parameters.
    // TODO(ZX-1413): Handle secondary GTF and CVT
    // TODO(stevensd): Support interlaced modes and margins
    uint32_t width = std.horizontal_resolution();
    uint32_t height = std.vertical_resolution(edid.edid_version, edid.edid_revision);
    uint32_t v_rate = std.vertical_freq() + 60;

    if (!width || !height || !v_rate) {
        return;
    }

    const timing_params_t* dmt_timing = internal::dmt_timings;
    for (unsigned i = 0; i < internal::dmt_timings_count; i++, dmt_timing++) {
        if (dmt_timing->horizontal_addressable == width
                && dmt_timing->vertical_addressable == height
                && ((dmt_timing->vertical_refresh_e2 + 50) / 100) == v_rate) {
            *params = *dmt_timing;
            return;
        }
    }

    // Default values for GFT variables
    static constexpr uint32_t kCellGran = 8;
    static constexpr uint32_t kMinPorch = 1;
    static constexpr uint32_t kVsyncRequired = 3;
    static constexpr uint32_t kHsyncPercent = 8;
    static constexpr uint32_t kMinVsyncPlusBpUs = 550;
    static constexpr uint32_t kM = 600;
    static constexpr uint32_t kC = 40;
    static constexpr uint32_t kK = 128;
    static constexpr uint32_t kJ = 20;
    static constexpr uint32_t kCPrime = ((kC - kJ) * kK / 256) + kJ;
    static constexpr uint32_t kMPrime = (kK * kM) / 256;

    uint32_t h_pixels_rnd = round_div(width, kCellGran) * kCellGran;
    double h_period_est =
            (1000000.0 - kMinVsyncPlusBpUs * v_rate) / (v_rate * (height + kMinPorch));
    uint32_t vsync_bp = round_div(kMinVsyncPlusBpUs, h_period_est);
    uint32_t v_total_lines = height + vsync_bp + kMinPorch;
    double v_field_rate_est = 1000000.0 / (h_period_est * v_total_lines);
    double h_period = (1.0 * h_period_est * v_field_rate_est) / v_rate;
    double v_field_rate = 1000000.0 / h_period / v_total_lines;
    double ideal_duty_cycle = kCPrime - (kMPrime * h_period_est / 1000);
    uint32_t h_blank_pixels = 2 * kCellGran * round_div(
            h_pixels_rnd * ideal_duty_cycle, (100 - ideal_duty_cycle) * (2 * kCellGran));
    uint32_t total_pixels = h_pixels_rnd + h_blank_pixels;
    double pixel_freq = total_pixels / h_period;

    params->pixel_freq_10khz = (uint32_t) (pixel_freq * 100 + 50);
    params->horizontal_addressable = h_pixels_rnd;
    params->horizontal_sync_pulse =
            round_div(kHsyncPercent * total_pixels, 100 * kCellGran) * kCellGran;
    params->horizontal_front_porch = h_blank_pixels / 2 - params->horizontal_sync_pulse;
    params->horizontal_blanking = h_blank_pixels;
    params->vertical_addressable = height;
    params->vertical_front_porch = kMinPorch;
    params->vertical_sync_pulse = kVsyncRequired;
    params->vertical_blanking = vsync_bp + kMinPorch;

    // TODO(ZX-1413): Set these depending on if we use default/secondary GTF
    params->flags = timing_params::kPositiveVsync;

    params->vertical_refresh_e2 = static_cast<uint32_t>(v_field_rate * 100 + .5);
}

Edid::timing_iterator& Edid::timing_iterator::operator++() {
    bool done = false;
    while (block_idx_ != UINT8_MAX && !done) {
        params_ = {};
        done = true;
        Advance();

        // If either of these are 0, then the timing value is definitely wrong
        if (params_.vertical_addressable == 0
                || params_.horizontal_addressable == 0) {
            done = false;
        }
    }
    return *this;
}

void Edid::timing_iterator::Advance() {
    params_ = {};
    // The order in which timings are returned is:
    //   1) Detailed timings in base edid
    //   2) Timings in CEA data blocks
    //       - Detailed timings
    //       - Short Video Descriptors
    //   3) Standard timings in base edid
    // TODO: Standart timings in descriptors
    // TODO: GTF/CVT timings in descriptors/monitor range limits
    // TODO: Established timings

    if (block_idx_ == 0) {
        if (timing_idx_ == 3) {
            timing_idx_ = UINT32_MAX;
            block_idx_++;
        } else {
            timing_idx_++;
            if (edid_->base_edid_.detailed_timings[timing_idx_].pixel_clock_10khz == 0) {
                // If pixel_clock_10khz is 0, then we've seen all the DTDs in the base
                // edid and are now looking at some other descriptor block.
                timing_idx_ = UINT32_MAX;
                block_idx_++;
            } else {
                convert_dtd_to_timing(
                        *(edid_->base_edid_.detailed_timings + timing_idx_), &params_);
                return;
            }
        }
    }

    while (block_idx_ < (edid_->len_ / kBlockSize)) {
        CeaEdidTimingExtension cea_extn_block;
        if (!edid_->GetBlock(block_idx_, &cea_extn_block) || cea_extn_block.dtd_start_idx == 0) {
            // Skip blocks which aren't the right type or which don't have any DTDs
            block_idx_++;
            timing_idx_ = UINT32_MAX;
            continue;
        }

        timing_idx_++;
        uint32_t modes_to_skip = timing_idx_;
        size_t dbc_end = cea_extn_block.dtd_start_idx - offsetof(CeaEdidTimingExtension, payload);
        size_t offset = dbc_end;

        // First, look at the detailed timing descriptors
        while (offset + sizeof(DetailedTimingDescriptor)
                <= sizeof(CeaEdidTimingExtension::payload)) {
            auto dtd = reinterpret_cast<DetailedTimingDescriptor*>(cea_extn_block.payload + offset);
            if (dtd->pixel_clock_10khz != 0) {
                if (modes_to_skip == 0) {
                    convert_dtd_to_timing(*dtd, &params_);
                    return;
                }
                modes_to_skip--;
            } else {
                break;
            }
            offset += sizeof(DetailedTimingDescriptor);
        }

        // Then look through the data blocks for any short video descriptors
        offset = 0;
        while (offset < dbc_end) {
            auto* dblk = reinterpret_cast<DataBlock*>(cea_extn_block.payload + offset);
            if (dblk->type() == ShortVideoDescriptor::kType) {
                for (unsigned i = 0; i < dblk->length(); i++) {
                    uint32_t idx = dblk->payload.video[i].standard_mode_idx() - 1;
                    if (idx >= internal::cea_timings_count) {
                        continue;
                    }
                    if (modes_to_skip == 0) {
                        params_ = internal::cea_timings[idx];
                        return;
                    }

                    // For timings with refresh rates that are multiples of 6, there are
                    // corresponding timings adjusted by a factor of 1000/1001.
                    uint32_t rounded_refresh =
                            (internal::cea_timings[idx].vertical_refresh_e2 + 99) / 100;
                    if (rounded_refresh % 6 == 0) {
                        if (modes_to_skip == 1) {
                            params_ = internal::cea_timings[idx];
                            double clock = params_.pixel_freq_10khz;
                            double refresh = params_.vertical_refresh_e2;
                            // 240/480 height entries are already multipled by 1000/1001
                            double mult = params_.vertical_addressable == 240
                                    || params_.vertical_addressable == 480
                                    ? 1.001 : (1000. / 1001.);
                            params_.pixel_freq_10khz = static_cast<uint32_t>(round(clock * mult));
                            params_.vertical_refresh_e2 =
                                    static_cast<uint32_t>(round(refresh * mult));
                            return;
                        }
                        modes_to_skip -= 2;
                    } else {
                        modes_to_skip--;
                    }
                }
            }
            offset += (dblk->length() + 1); // length doesn't include the data block header byte
        }

        // If all the modes have been processed, go to the next block
        block_idx_++;
        timing_idx_ = UINT32_MAX;
    }

    if (block_idx_ == (edid_->len_ / kBlockSize)) {
        while (++timing_idx_ < fbl::count_of(edid_->base_edid_.standard_timings)) {
            const StandardTimingDescriptor* desc = edid_->base_edid_.standard_timings + timing_idx_;
            if (desc->byte1 == 0x01 && desc->byte2 == 0x01) {
                continue;
            }
            convert_std_to_timing(edid_->base_edid_, *desc, &params_);
            return;
        }

        timing_idx_ = UINT32_MAX;
        block_idx_ = UINT8_MAX;
    }
}

void Edid::Print(void (*print_fn)(const char* str)) const {
    char str_buf[128];
    print_fn("Raw edid:\n");
    for (auto i = 0; i < edid_length(); i++) {
        constexpr int kBytesPerLine = 16;
        char *b = str_buf;
        if (i % kBytesPerLine == 0) {
            b += sprintf(b, "%04x: ", i);
        }
        sprintf(b, "%02x%s", edid_bytes()[i],
                i % kBytesPerLine == kBytesPerLine - 1 ? "\n" : " ");
        print_fn(str_buf);
    }
}

} // namespace edid
