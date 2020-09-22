// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/edid/edid.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <iterator>
#include <memory>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>

#include "eisa_vid_lut.h"

namespace {

template <typename T>
bool base_validate(const T* block) {
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

uint32_t round_div(double num, double div) { return (uint32_t)((num / div) + .5); }

}  // namespace

namespace edid {

bool BaseEdid::validate() const {
  static const uint8_t kEdidHeader[8] = {0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0};
  return base_validate<BaseEdid>(this) && memcmp(header, kEdidHeader, sizeof(kEdidHeader)) == 0;
}

bool CeaEdidTimingExtension::validate() const {
  if (!(dtd_start_idx <= sizeof(payload) && base_validate<CeaEdidTimingExtension>(this))) {
    return false;
  }

  // If this is zero, there is no DTDs present and no non-DTD data.
  if (dtd_start_idx == 0) {
    return true;
  }

  if (dtd_start_idx > 0 && dtd_start_idx < offsetof(CeaEdidTimingExtension, payload)) {
    return false;
  }

  size_t offset = 0;
  size_t dbc_end = dtd_start_idx - offsetof(CeaEdidTimingExtension, payload);
  while (offset < dbc_end) {
    const DataBlock* data_block = reinterpret_cast<const DataBlock*>(payload + offset);
    offset += (1 + data_block->length());  // Length doesn't include the header
    // Check that the block doesn't run past the end if the dbc
    if (offset > dbc_end) {
      return false;
    }
  }
  return true;
}

bool Edid::Init(void* ctx, ddc_i2c_transact transact, const char** err_msg) {
  uint8_t segment_address = 0;
  uint8_t segment_offset = 0;
  ddc_i2c_msg_t msgs[3] = {
      {.is_read = false, .addr = kDdcSegmentI2cAddress, .buf = &segment_address, .length = 1},
      {.is_read = false, .addr = kDdcDataI2cAddress, .buf = &segment_offset, .length = 1},
      {.is_read = true, .addr = kDdcDataI2cAddress, .buf = nullptr, .length = kBlockSize},
  };

  BaseEdid base_edid;
  msgs[2].buf = reinterpret_cast<uint8_t*>(&base_edid);
  // + 1 to skip trying to set the segment for the first block
  if (!transact(ctx, msgs + 1, 2)) {
    *err_msg = "Failed to read base edid";
    return false;
  } else if (!base_edid.validate()) {
    *err_msg = "Failed to validate base edid";
    return false;
  }

  uint16_t edid_length = static_cast<uint16_t>((base_edid.num_extensions + 1) * kBlockSize);
  fbl::AllocChecker ac;
  edid_bytes_ = std::unique_ptr<uint8_t[]>(new (&ac) uint8_t[edid_length]);
  if (!ac.check()) {
    *err_msg = "Failed to allocate edid storage";
    return false;
  }

  memcpy(edid_bytes_.get(), reinterpret_cast<void*>(&base_edid), kBlockSize);
  for (uint8_t i = 1; i && i <= base_edid.num_extensions; i++) {
    *msgs[0].buf = i / 2;
    *msgs[1].buf = i % 2 ? kBlockSize : 0;
    msgs[2].buf = edid_bytes_.get() + i * kBlockSize;
    bool include_segment = i % 2;
    if (!transact(ctx, msgs + include_segment, 3 - include_segment)) {
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
  if (!(base_edid_ = GetBlock<BaseEdid>(0)) || !base_edid_->validate()) {
    *err_msg = "Failed to validate base edid";
    return false;
  }
  if (((base_edid_->num_extensions + 1) * kBlockSize) != len) {
    *err_msg = "Bad extension count";
    return false;
  }
  if (!base_edid_->digital()) {
    *err_msg = "Analog displays not supported";
    return false;
  }

  for (uint8_t i = 1; i < len / kBlockSize; i++) {
    if (bytes_[i * kBlockSize] == CeaEdidTimingExtension::kTag) {
      if (!GetBlock<CeaEdidTimingExtension>(i)->validate()) {
        *err_msg = "Failed to validate extensions";
        return false;
      }
    }
  }

  monitor_serial_[0] = monitor_name_[0] = '\0';
  for (auto it = descriptor_iterator(this); it.is_valid(); ++it) {
    char* dest;
    if (it->timing.pixel_clock_10khz != 0) {
      continue;
    } else if (it->monitor.type == Descriptor::Monitor::kName) {
      dest = monitor_name_;
    } else if (it->monitor.type == Descriptor::Monitor::kSerial) {
      dest = monitor_serial_;
    } else {
      continue;
    }

    uint32_t len;
    for (len = 0; len < sizeof(Descriptor::Monitor::data) && it->monitor.data[len] != 0x0A; ++len) {
      // Empty body
    }

    snprintf(dest, len + 1, "%s", it->monitor.data);
  }

  // If we didn't find a valid serial descriptor, use the base serial number
  if (monitor_serial_[0] == '\0') {
    sprintf(monitor_serial_, "%d", base_edid_->serial_number);
  }

  uint8_t c1 = static_cast<uint8_t>(((base_edid_->manufacturer_id1 & 0x7c) >> 2) + 'A' - 1);
  uint8_t c2 = static_cast<uint8_t>(
      (((base_edid_->manufacturer_id1 & 0x03) << 3) | (base_edid_->manufacturer_id2 & 0xe0) >> 5) +
      'A' - 1);
  uint8_t c3 = static_cast<uint8_t>(((base_edid_->manufacturer_id2 & 0x1f)) + 'A' - 1);

  manufacturer_id_[0] = c1;
  manufacturer_id_[1] = c2;
  manufacturer_id_[2] = c3;
  manufacturer_id_[3] = '\0';
  manufacturer_name_ = lookup_eisa_vid(EISA_ID(c1, c2, c3));

  return true;
}

template <typename T>
const T* Edid::GetBlock(uint8_t block_num) const {
  const uint8_t* bytes = bytes_ + block_num * kBlockSize;
  return bytes[0] == T::kTag ? reinterpret_cast<const T*>(bytes) : nullptr;
}

bool Edid::is_hdmi() const {
  data_block_iterator dbs(this);
  if (!dbs.is_valid() || dbs.cea_revision() < 0x03) {
    return false;
  }

  do {
    if (dbs->type() == VendorSpecificBlock::kType) {
      // HDMI's 24-bit IEEE registration is 0x000c03 - vendor_number is little endian
      if (dbs->payload.vendor.vendor_number[0] == 0x03 &&
          dbs->payload.vendor.vendor_number[1] == 0x0c &&
          dbs->payload.vendor.vendor_number[2] == 0x00) {
        return true;
      }
    }
  } while ((++dbs).is_valid());
  return false;
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
  params->flags = (dtd.vsync_polarity() ? timing_params::kPositiveVsync : 0) |
                  (dtd.hsync_polarity() ? timing_params::kPositiveHsync : 0) |
                  (dtd.interlaced() ? timing_params::kInterlaced : 0);

  double total_pxls = (params->horizontal_addressable + params->horizontal_blanking) *
                      (params->vertical_addressable + params->vertical_blanking);
  double pixel_clock_hz = params->pixel_freq_10khz * 1000 * 10;
  params->vertical_refresh_e2 = static_cast<uint32_t>(round(100 * pixel_clock_hz / total_pxls));
}

void convert_std_to_timing(const BaseEdid& edid, const StandardTimingDescriptor& std,
                           timing_params* params) {
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
    if (dmt_timing->horizontal_addressable == width && dmt_timing->vertical_addressable == height &&
        ((dmt_timing->vertical_refresh_e2 + 50) / 100) == v_rate) {
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
  double h_period_est = (1000000.0 - kMinVsyncPlusBpUs * v_rate) / (v_rate * (height + kMinPorch));
  uint32_t vsync_bp = round_div(kMinVsyncPlusBpUs, h_period_est);
  uint32_t v_total_lines = height + vsync_bp + kMinPorch;
  double v_field_rate_est = 1000000.0 / (h_period_est * v_total_lines);
  double h_period = (1.0 * h_period_est * v_field_rate_est) / v_rate;
  double v_field_rate = 1000000.0 / h_period / v_total_lines;
  double ideal_duty_cycle = kCPrime - (kMPrime * h_period_est / 1000);
  uint32_t h_blank_pixels =
      2 * kCellGran *
      round_div(h_pixels_rnd * ideal_duty_cycle, (100 - ideal_duty_cycle) * (2 * kCellGran));
  uint32_t total_pixels = h_pixels_rnd + h_blank_pixels;
  double pixel_freq = total_pixels / h_period;

  params->pixel_freq_10khz = (uint32_t)(pixel_freq * 100 + 50);
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

timing_iterator& timing_iterator::operator++() {
  while (state_ != kDone) {
    Advance();
    // If either of these are 0, then the timing value is definitely wrong
    if (params_.vertical_addressable != 0 && params_.horizontal_addressable != 0) {
      break;
    }
  }
  return *this;
}

void timing_iterator::Advance() {
  if (state_ == kDtds) {
    while (descriptors_.is_valid()) {
      if (descriptors_->timing.pixel_clock_10khz != 0) {
        convert_dtd_to_timing(descriptors_->timing, &params_);
        ++descriptors_;
        return;
      }
      ++descriptors_;
    }
    state_ = kSvds;
    state_index_ = UINT16_MAX;
  }

  if (state_ == kSvds) {
    while (dbs_.is_valid()) {
      if (dbs_->type() == ShortVideoDescriptor::kType) {
        state_index_++;
        uint32_t modes_to_skip = state_index_;
        for (unsigned i = 0; i < dbs_->length(); i++) {
          uint32_t idx = dbs_->payload.video[i].standard_mode_idx() - 1;
          if (idx >= internal::cea_timings_count) {
            continue;
          }
          if (modes_to_skip == 0) {
            params_ = internal::cea_timings[idx];
            return;
          }

          // For timings with refresh rates that are multiples of 6, there are
          // corresponding timings adjusted by a factor of 1000/1001.
          uint32_t rounded_refresh = (internal::cea_timings[idx].vertical_refresh_e2 + 99) / 100;
          if (rounded_refresh % 6 == 0) {
            if (modes_to_skip == 1) {
              params_ = internal::cea_timings[idx];
              double clock = params_.pixel_freq_10khz;
              double refresh = params_.vertical_refresh_e2;
              // 240/480 height entries are already multipled by 1000/1001
              double mult =
                  params_.vertical_addressable == 240 || params_.vertical_addressable == 480
                      ? 1.001
                      : (1000. / 1001.);
              params_.pixel_freq_10khz = static_cast<uint32_t>(round(clock * mult));
              params_.vertical_refresh_e2 = static_cast<uint32_t>(round(refresh * mult));
              return;
            }
            modes_to_skip -= 2;
          } else {
            modes_to_skip--;
          }
        }
      }

      ++dbs_;
      // Reset the index for either the next SVD block or the STDs.
      state_index_ = UINT16_MAX;
    }

    state_ = kStds;
  }

  if (state_ == kStds) {
    while (++state_index_ < std::size(edid_->base_edid_->standard_timings)) {
      const StandardTimingDescriptor* desc = edid_->base_edid_->standard_timings + state_index_;
      if (desc->byte1 == 0x01 && desc->byte2 == 0x01) {
        continue;
      }
      convert_std_to_timing(*edid_->base_edid_, *desc, &params_);
      return;
    }

    state_ = kDone;
  }
}

audio_data_block_iterator& audio_data_block_iterator::operator++() {
  while (dbs_.is_valid()) {
    uint32_t num_sads = static_cast<uint32_t>(dbs_->length() / sizeof(ShortAudioDescriptor));
    if (dbs_->type() != ShortAudioDescriptor::kType || ++sad_idx_ > num_sads) {
      ++dbs_;
      sad_idx_ = UINT8_MAX;
      continue;
    }
    descriptor_ = dbs_->payload.audio[sad_idx_];
    return *this;
  }

  edid_ = nullptr;
  return *this;
}

Edid::descriptor_iterator& Edid::descriptor_iterator::operator++() {
  if (!edid_) {
    return *this;
  }

  if (block_idx_ == 0) {
    descriptor_idx_++;

    if (descriptor_idx_ < std::size(edid_->base_edid_->detailed_descriptors)) {
      descriptor_ = edid_->base_edid_->detailed_descriptors + descriptor_idx_;
      if (descriptor_->timing.pixel_clock_10khz != 0 || descriptor_->monitor.type != 0x10) {
        return *this;
      }
    }

    block_idx_++;
    descriptor_idx_ = UINT32_MAX;
  }

  while (block_idx_ < (edid_->len_ / kBlockSize)) {
    auto cea_extn_block = edid_->GetBlock<CeaEdidTimingExtension>(block_idx_);
    size_t offset = sizeof(CeaEdidTimingExtension::payload);
    if (cea_extn_block &&
        cea_extn_block->dtd_start_idx > offsetof(CeaEdidTimingExtension, payload)) {
      offset = cea_extn_block->dtd_start_idx - offsetof(CeaEdidTimingExtension, payload);
    }

    descriptor_idx_++;
    offset += sizeof(Descriptor) * descriptor_idx_;

    // Return if the descriptor is within bounds and either a timing descriptor or not
    // a dummy monitor descriptor, otherwise advance to the next block
    if (offset + sizeof(DetailedTimingDescriptor) <= sizeof(CeaEdidTimingExtension::payload)) {
      descriptor_ = reinterpret_cast<const Descriptor*>(cea_extn_block->payload + offset);
      if (descriptor_->timing.pixel_clock_10khz != 0 ||
          descriptor_->monitor.type != Descriptor::Monitor::kDummyType) {
        return *this;
      }
    }

    block_idx_++;
    descriptor_idx_ = UINT32_MAX;
  }

  edid_ = nullptr;
  return *this;
}

Edid::data_block_iterator::data_block_iterator(const Edid* edid) : edid_(edid) {
  ++(*this);
  if (is_valid()) {
    cea_revision_ = edid_->GetBlock<CeaEdidTimingExtension>(block_idx_)->revision_number;
  }
}

Edid::data_block_iterator& Edid::data_block_iterator::operator++() {
  if (!edid_) {
    return *this;
  }

  while (block_idx_ < (edid_->len_ / kBlockSize)) {
    auto cea_extn_block = edid_->GetBlock<CeaEdidTimingExtension>(block_idx_);
    size_t dbc_end = 0;
    if (cea_extn_block &&
        cea_extn_block->dtd_start_idx > offsetof(CeaEdidTimingExtension, payload)) {
      dbc_end = cea_extn_block->dtd_start_idx - offsetof(CeaEdidTimingExtension, payload);
    }

    db_idx_++;
    uint32_t db_to_skip = db_idx_;

    uint32_t offset = 0;
    while (offset < dbc_end) {
      auto* dblk = reinterpret_cast<const DataBlock*>(cea_extn_block->payload + offset);
      if (db_to_skip == 0) {
        db_ = dblk;
        return *this;
      }
      db_to_skip--;
      offset += (dblk->length() + 1);  // length doesn't include the data block header byte
    }

    block_idx_++;
    db_idx_ = UINT32_MAX;
  }

  edid_ = nullptr;
  return *this;
}

void Edid::Print(void (*print_fn)(const char* str)) const {
  char str_buf[128];
  print_fn("Raw edid:\n");
  for (auto i = 0; i < edid_length(); i++) {
    constexpr int kBytesPerLine = 16;
    char* b = str_buf;
    if (i % kBytesPerLine == 0) {
      b += sprintf(b, "%04x: ", i);
    }
    sprintf(b, "%02x%s", edid_bytes()[i], i % kBytesPerLine == kBytesPerLine - 1 ? "\n" : " ");
    print_fn(str_buf);
  }
}

bool Edid::supports_basic_audio() const {
  uint8_t block_idx = 1;  // Skip block 1, since it can't be a CEA block
  while (block_idx < (len_ / kBlockSize)) {
    auto cea_extn_block = GetBlock<CeaEdidTimingExtension>(block_idx);
    if (cea_extn_block && cea_extn_block->revision_number >= 2) {
      return cea_extn_block->basic_audio();
    }
    block_idx++;
  }
  return false;
}

}  // namespace edid
