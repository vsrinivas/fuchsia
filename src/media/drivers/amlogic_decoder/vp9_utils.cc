// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vp9_utils.h"

#include <byteswap.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <ddk/debug.h>

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
        zxlogf(ERROR, "Unsupported bytes_per_framesize: %d\n", bytes_per_framesize);
        frame_sizes.clear();
        return frame_sizes;
    }
    total_size += sub_frame_size;
    if (total_size > frame_size) {
      zxlogf(ERROR, "Total superframe size too large: %u > %u\n", total_size, frame_size);
      frame_sizes.clear();
      return frame_sizes;
    }
    frame_sizes.push_back(sub_frame_size);
  }
  return frame_sizes;
}

void SplitSuperframe(const uint8_t* data, uint32_t frame_size, std::vector<uint8_t>* output_vector,
                     std::vector<uint32_t>* superframe_byte_sizes) {
  std::vector<uint32_t> frame_sizes = TryParseSuperframeHeader(data, frame_size);

  if (frame_sizes.empty())
    frame_sizes.push_back(frame_size);
  uint32_t frame_offset = 0;
  uint32_t total_frame_bytes = 0;
  for (auto& size : frame_sizes) {
    total_frame_bytes += size;
  }
  const uint32_t kOutputHeaderSize = 16;
  uint32_t output_offset = output_vector->size();
  // This can be called multiple times on the same output_vector overall, but
  // should be amortized O(1), since resizing larger inserts elements at the end
  // and inserting elements at the end is amortized O(1) for std::vector.
  output_vector->resize(output_offset + total_frame_bytes + kOutputHeaderSize * frame_sizes.size());
  uint8_t* output = &(*output_vector)[output_offset];
  for (auto& size : frame_sizes) {
    ZX_DEBUG_ASSERT(output + kOutputHeaderSize - output_vector->data() <=
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
      superframe_byte_sizes->push_back(size + kOutputHeaderSize);
    }
  }
  ZX_DEBUG_ASSERT(output - output_vector->data() == static_cast<int64_t>(output_vector->size()));
}
