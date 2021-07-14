// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vp9_utils.h"

#include <byteswap.h>
#include <lib/ddk/debug.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include "macros.h"

namespace amlogic_decoder {

std::vector<uint32_t> TryParseSuperframeHeader(const uint8_t* data, uint32_t frame_size) {
  std::vector<uint32_t> frame_sizes;
  if (frame_size < 1)
    return frame_sizes;
  uint8_t superframe_header = data[frame_size - 1];

  // Split out superframes into separate frames - see
  // https://storage.googleapis.com/downloads.webmproject.org/docs/vp9/vp9-bitstream-specification-v0.6-20160331-draft.pdf
  // Annex B.
  if ((superframe_header & 0xe0) != 0xc0)
    return frame_sizes;
  uint8_t bytes_per_framesize = ((superframe_header >> 3) & 3) + 1;
  uint8_t superframe_count = (superframe_header & 7) + 1;
  uint32_t superframe_index_size = 2 + bytes_per_framesize * superframe_count;
  if (superframe_index_size > frame_size)
    return frame_sizes;
  if (data[frame_size - superframe_index_size] != superframe_header) {
    return frame_sizes;
  }
  const uint8_t* index_data = &data[frame_size - superframe_index_size + 1];
  uint32_t total_size = 0;
  for (uint32_t i = 0; i < superframe_count; i++) {
    uint32_t sub_frame_size;
    switch (bytes_per_framesize) {
      case 1:
        sub_frame_size = index_data[i];
        break;
      case 2:
        sub_frame_size = reinterpret_cast<const uint16_t*>(index_data)[i];
        break;
      case 3:
        sub_frame_size = 0;
        for (uint32_t j = 0; j < 3; ++j) {
          sub_frame_size |= static_cast<uint32_t>(index_data[i * 3 + j]) << (j * 8);
        }
        break;
      case 4:
        sub_frame_size = reinterpret_cast<const uint32_t*>(index_data)[i];
        break;
      default:
        zxlogf(ERROR, "Unsupported bytes_per_framesize: %d", bytes_per_framesize);
        frame_sizes.clear();
        return frame_sizes;
    }
    total_size += sub_frame_size;
    if (total_size > frame_size) {
      zxlogf(ERROR, "Total superframe size too large: %u > %u", total_size, frame_size);
      frame_sizes.clear();
      return frame_sizes;
    }
    frame_sizes.push_back(sub_frame_size);
  }
  return frame_sizes;
}

void SplitSuperframe(const uint8_t* data, uint32_t frame_size, std::vector<uint8_t>* output_vector,
                     std::vector<uint32_t>* superframe_byte_sizes, bool like_secmem) {
  std::vector<uint32_t> frame_sizes = TryParseSuperframeHeader(data, frame_size);

  if (frame_sizes.empty())
    frame_sizes.push_back(frame_size);
  uint32_t frame_offset = 0;
  uint32_t total_frame_bytes = 0;
  for (auto& size : frame_sizes) {
    total_frame_bytes += size;
  }
  ZX_DEBUG_ASSERT_MSG(total_frame_bytes <= frame_size, "total_frame_bytes: 0x%x frame_size: 0x%x",
                      total_frame_bytes, frame_size);
  uint32_t output_offset = output_vector->size();
  // This can be called multiple times on the same output_vector overall, but
  // should be amortized O(1), since resizing larger inserts elements at the end
  // and inserting elements at the end is amortized O(1) for std::vector.
  uint32_t output_vector_size_increase = kVp9AmlvHeaderSize * frame_sizes.size();
  if (like_secmem) {
    output_vector_size_increase += frame_size;
  } else {
    output_vector_size_increase += total_frame_bytes;
  }
  output_vector->resize(output_offset + output_vector_size_increase);
  uint8_t* output = &(*output_vector)[output_offset];
  for (auto& size : frame_sizes) {
    ZX_DEBUG_ASSERT(output + kVp9AmlvHeaderSize - output_vector->data() <=
                    static_cast<int64_t>(output_vector->size()));
    *reinterpret_cast<uint32_t*>(output) = bswap_32(size + 4);
    output += 4;
    *reinterpret_cast<uint32_t*>(output) = ~bswap_32(size + 4);
    output += 4;
    *output++ = 0;
    *output++ = 0;
    *output++ = 0;
    *output++ = 1;
    *output++ = 'A';
    *output++ = 'M';
    *output++ = 'L';
    *output++ = 'V';

    ZX_DEBUG_ASSERT(output + size - output_vector->data() <=
                    static_cast<int64_t>(output_vector->size()));
    memcpy(output, &data[frame_offset], size);
    output += size;
    frame_offset += size;
    if (superframe_byte_sizes) {
      superframe_byte_sizes->push_back(size + kVp9AmlvHeaderSize);
    }
  }
  if (like_secmem) {
    ZX_DEBUG_ASSERT(output - output_vector->data() + frame_size - total_frame_bytes ==
                    static_cast<int64_t>(output_vector->size()));
  } else {
    ZX_DEBUG_ASSERT(output - output_vector->data() == static_cast<int64_t>(output_vector->size()));
  }
}

fpromise::result<bool, fuchsia::media::StreamError> IsVp9KeyFrame(uint8_t frame_header_byte_0) {
  // We could make a bit-shifter class, but ... not really parsing that much here...
  uint8_t byte_0_shifter = frame_header_byte_0;
  uint8_t frame_marker = byte_0_shifter >> 6;
  byte_0_shifter <<= 2;
  if (frame_marker != kVp9FrameMarker) {
    LOG(ERROR, "frame marker not 2");
    return fpromise::error(fuchsia::media::StreamError::DECODER_DATA_PARSING);
  }
  uint8_t profile_low_bit = byte_0_shifter >> 7;
  byte_0_shifter <<= 1;
  uint8_t profile_high_bit = byte_0_shifter >> 7;
  byte_0_shifter <<= 1;
  uint8_t profile = (profile_high_bit << 1) | profile_low_bit;
  if (profile == 3) {
    uint8_t reserved_zero = byte_0_shifter >> 7;
    byte_0_shifter <<= 1;
    if (reserved_zero != 0) {
      LOG(ERROR, "reserved_zero not zero");
      return fpromise::error(fuchsia::media::StreamError::DECODER_DATA_PARSING);
    }
  }

  uint8_t show_existing_frame = byte_0_shifter >> 7;
  byte_0_shifter <<= 1;
  if (show_existing_frame) {
    // without having seen a keyframe, a show_existing_frame isn't going to find the frame it
    // wants to show.
    LOG(DEBUG, "show_existing_frame");
    return fpromise::ok(false);
  }
  ZX_DEBUG_ASSERT(!show_existing_frame);

  uint8_t frame_type = byte_0_shifter >> 7;
  byte_0_shifter <<= 1;
  if (frame_type != kVp9FrameTypeKeyFrame) {
    // without having seen a keyframe, a non-keyframe isn't going to be able to decode
    // properly, so skip.
    LOG(DEBUG, "frame_type != kVp9FrameTypeKeyFrame");
    return fpromise::ok(false);
  }
  ZX_DEBUG_ASSERT(frame_type == kVp9FrameTypeKeyFrame);

  return fpromise::ok(true);
}

}  // namespace amlogic_decoder
