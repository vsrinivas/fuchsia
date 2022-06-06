// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/codec/codecs/vaapi/avcc_processor.h"

#include <lib/fit/defer.h>

#include <limits>

#define LOG(x, ...) fprintf(stderr, __VA_ARGS__)

void AvccProcessor::ProcessOobBytes(const fuchsia::media::FormatDetails& format_details) {
  if (!format_details.has_oob_bytes())
    return;
  const auto& oob = format_details.oob_bytes();
  if (oob.empty()) {
    return;
  }

  // We need to deliver Annex B style SPS/PPS to this core codec, regardless of
  // what format the oob_bytes is in.

  // The oob_bytes can be in two different forms, which can be detected by
  // the value of the first byte:
  //
  // 0 - Annex B form already.  The 0 is the first byte of a start code.
  // 1 - AVCC form, which we'll convert to Annex B form.  AVCC version 1.  There
  //   is no AVCC version 0.
  // anything else - fail.
  //
  // In addition, we need to know if AVCC or not since we need to know whether
  // to add start code emulation prevention bytes or not.  And if it's AVCC,
  // how many bytes long the pseudo_nal_length field is - that field is before
  // each input NAL.

  // We already checked empty() above.
  ZX_DEBUG_ASSERT(oob.size() >= 1);
  switch ((oob)[0]) {
    case 0:
      is_avcc_ = false;
      return;
    case 1: {
      // This applies to both the oob data and the input packet payload data.
      // Both are AVCC, or both are AnnexB.
      is_avcc_ = true;

      /*
        AVCC OOB data layout (bits):
        [0] (8) - version 1
        [1] (8) - h264 profile #
        [2] (8) - compatible profile bits
        [3] (8) - h264 level (eg. 31 == "3.1")
        [4] (6) - reserved, can be set to all 1s
            (2) - pseudo_nal_length_field_bytes_ - 1
        [5] (3) - reserved, can be set to all 1s
            (5) - sps_count
              (16) - sps_bytes
              (8*sps_bytes) - SPS nal_unit_type (that byte) + SPS data as RBSP.
            (8) - pps_count
              (16) - pps_bytes
              (8*pps_bytes) - PPS nal_unit_type (that byte) + PPS data as RBSP.
      */

      // We accept 0 SPS and/or 0 PPS, but typically there's one of each.  At
      // minimum the oob buffer needs to be large enough to contain both the
      // sps_count and pps_count fields, which is a min of 7 bytes.
      if (oob.size() < 7) {
        LOG(ERROR, "oob->size() < 7");
        events_->onCoreCodecFailStream(fuchsia::media::StreamError::INVALID_INPUT_FORMAT_DETAILS);
        return;
      }
      // All pseudo-NALs in input packet payloads will use the
      // parsed count of bytes of the length field. Convert SPS/PPS inline to AnnexB format so we
      // can return it directly, as ParseVideo won't be called on this data.
      pseudo_nal_length_field_bytes_ = ((oob)[4] & 0x3) + 1;
      uint32_t sps_count = (oob)[5] & 0x1F;
      uint32_t offset = 6;
      std::vector<uint8_t> accumulation;
      for (uint32_t i = 0; i < sps_count; ++i) {
        if (offset + 2 > oob.size()) {
          LOG(ERROR, "offset + 2 > oob->size()");
          events_->onCoreCodecFailStream(fuchsia::media::StreamError::INVALID_INPUT_FORMAT_DETAILS);
          return;
        }
        uint32_t sps_length = (oob)[offset] * 256 + (oob)[offset + 1];
        if (offset + 2 + sps_length > oob.size()) {
          LOG(ERROR, "offset + 2 + sps_length > oob->size()");
          events_->onCoreCodecFailStream(fuchsia::media::StreamError::INVALID_INPUT_FORMAT_DETAILS);
          return;
        }
        offset += 2;  // sps_bytes
        accumulation.push_back(0);
        accumulation.push_back(0);
        accumulation.push_back(0);
        accumulation.push_back(1);
        for (uint32_t i = 0; i < sps_length; i++) {
          accumulation.push_back(oob.data()[offset + i]);
        }
        offset += sps_length;
      }
      if (offset + 1 > oob.size()) {
        LOG(ERROR, "offset + 1 > oob->size()");
        events_->onCoreCodecFailStream(fuchsia::media::StreamError::INVALID_INPUT_FORMAT_DETAILS);
        return;
      }
      uint32_t pps_count = (oob)[offset++];
      for (uint32_t i = 0; i < pps_count; ++i) {
        if (offset + 2 > oob.size()) {
          LOG(ERROR, "offset + 2 > oob->size()");
          events_->onCoreCodecFailStream(fuchsia::media::StreamError::INVALID_INPUT_FORMAT_DETAILS);
          return;
        }
        uint32_t pps_length = (oob)[offset] * 256 + (oob)[offset + 1];
        if (offset + 2 + pps_length > oob.size()) {
          LOG(ERROR, "offset + 2 + pps_length > oob->size()");
          events_->onCoreCodecFailStream(fuchsia::media::StreamError::INVALID_INPUT_FORMAT_DETAILS);
          return;
        }
        offset += 2;  // pps_bytes
        accumulation.push_back(0);
        accumulation.push_back(0);
        accumulation.push_back(0);
        accumulation.push_back(1);
        for (uint32_t i = 0; i < pps_length; i++) {
          accumulation.push_back(oob.data()[offset + i]);
        }
        offset += pps_length;
      }

      bool returned_buffer = false;
      auto return_input_packet =
          fit::defer_callback(fit::closure([&returned_buffer] { returned_buffer = true; }));
      decode_annex_b_(
          media::DecoderBuffer(accumulation, nullptr, 0u, std::move(return_input_packet)));
      ZX_ASSERT(returned_buffer);
      return;
    }
    default:
      LOG(ERROR, "unexpected first oob byte");
      events_->onCoreCodecFailStream(fuchsia::media::StreamError::INVALID_INPUT_FORMAT_DETAILS);
      return;
  }
}

std::vector<uint8_t> AvccProcessor::ParseVideoAvcc(const uint8_t* data, size_t data_size) const {
  if (data_size > std::numeric_limits<uint32_t>::max()) {
    events_->onCoreCodecFailStream(fuchsia::media::StreamError::DECODER_UNKNOWN);
    return {};
  }
  const uint32_t length = static_cast<uint32_t>(data_size);

  // So far, the "avcC"/"AVCC" we've seen has emulation prevention bytes on it
  // already.  So we don't add those here.  But if we did need to add them, we'd
  // add them here.

  // For now we assume the heap is pretty fast and doesn't mind the size thrash,
  // but maybe we'll want to keep a buffer around (we'll optimize only if/when
  // we determine this is actually a problem).  We only actually use this buffer
  // if is_avcc_ (which is not uncommon).

  // We do parse more than one pseudo_nal per input packet.
  //
  // No splitting NALs across input packets, for now.
  //
  // TODO(dustingreen): Allow splitting NALs across input packets (not a small
  // change).  Probably also move into a source_set for sharing with other
  // CodecAdapter(s).

  // Count the input pseudo_nal(s)
  uint32_t pseudo_nal_count = 0;
  uint32_t i = 0;
  while (i < length) {
    if (i + pseudo_nal_length_field_bytes_ > length) {
      LOG(ERROR, "i + pseudo_nal_length_field_bytes_ > length");
      events_->onCoreCodecFailStream(fuchsia::media::StreamError::DECODER_UNKNOWN);
      return {};
    }
    // Read pseudo_nal_length field, which is a field which can be 1-4 bytes
    // long because AVCC/avcC.
    uint32_t pseudo_nal_length = 0;
    for (uint32_t length_byte = 0; length_byte < pseudo_nal_length_field_bytes_; ++length_byte) {
      pseudo_nal_length = pseudo_nal_length * 256 + data[i + length_byte];
    }
    i += pseudo_nal_length_field_bytes_;
    if (i + pseudo_nal_length > length) {
      LOG(ERROR, "i + pseudo_nal_length > length");
      events_->onCoreCodecFailStream(fuchsia::media::StreamError::DECODER_UNKNOWN);
      return {};
    }
    i += pseudo_nal_length;
    ++pseudo_nal_count;
  }

  static constexpr uint32_t kStartCodeBytes = 4;
  uint32_t local_length = length - pseudo_nal_count * pseudo_nal_length_field_bytes_ +
                          pseudo_nal_count * kStartCodeBytes;
  std::vector<uint8_t> local_buffer(local_length);
  uint8_t* local_data = local_buffer.data();

  i = 0;
  uint32_t o = 0;
  while (i < length) {
    if (i + pseudo_nal_length_field_bytes_ > length) {
      LOG(ERROR, "i + pseudo_nal_length_field_bytes_ > length");
      events_->onCoreCodecFailStream(fuchsia::media::StreamError::DECODER_UNKNOWN);
      return {};
    }
    uint32_t pseudo_nal_length = 0;
    for (uint32_t length_byte = 0; length_byte < pseudo_nal_length_field_bytes_; ++length_byte) {
      pseudo_nal_length = pseudo_nal_length * 256 + data[i + length_byte];
    }
    i += pseudo_nal_length_field_bytes_;
    if (i + pseudo_nal_length > length) {
      LOG(ERROR, "i + pseudo_nal_length > length");
      events_->onCoreCodecFailStream(fuchsia::media::StreamError::DECODER_UNKNOWN);
      return {};
    }

    local_data[o++] = 0;
    local_data[o++] = 0;
    local_data[o++] = 0;
    local_data[o++] = 1;

    memcpy(&local_data[o], &data[i], pseudo_nal_length);
    o += pseudo_nal_length;
    i += pseudo_nal_length;
  }
  ZX_DEBUG_ASSERT(o == local_length);
  ZX_DEBUG_ASSERT(i == length);
  return local_buffer;
}
