// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "h264_utils.h"

#include <cstring>

std::vector<std::vector<uint8_t>> SplitNalUnits(const uint8_t* start_data, uint32_t size) {
  std::vector<std::vector<uint8_t>> out_vector;

  const uint8_t* this_nal_start = start_data;
  while (true) {
    if (size < 3)
      return out_vector;
    uint8_t start_code[3] = {0, 0, 1};
    // Add 2 to ensure the next start code found isn't the start of this nal
    // unit.
    uint8_t* next_nal_start =
        static_cast<uint8_t*>(memmem(this_nal_start + 2, size - 2, start_code, sizeof(start_code)));
    if (next_nal_start && next_nal_start[-1] == 0)
      next_nal_start--;
    uint32_t data_size = next_nal_start ? next_nal_start - this_nal_start : size;
    if (data_size > 0) {
      std::vector<uint8_t> new_data(data_size);
      memcpy(new_data.data(), this_nal_start, data_size);
      out_vector.push_back(std::move(new_data));
    }

    if (!next_nal_start) {
      return out_vector;
    }

    size -= data_size;
    this_nal_start = next_nal_start;
  }
}

uint8_t GetNalUnitType(const std::vector<uint8_t>& nal_unit) {
  // Also works with 4-byte startcodes.
  uint8_t start_code[3] = {0, 0, 1};
  uint8_t* this_start = static_cast<uint8_t*>(
      memmem(nal_unit.data(), nal_unit.size(), start_code, sizeof(start_code)));
  if (!this_start)
    return 0;
  return this_start[sizeof(start_code)] & 0xf;
}
