// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_UTILS_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_UTILS_H_

#include <lib/stdcompat/span.h>

#include <cstdint>
#include <vector>

namespace amlogic_decoder {

std::vector<std::vector<uint8_t>> SplitNalUnits(const uint8_t* start_data, uint32_t size);
uint8_t GetNalUnitType(cpp20::span<const uint8_t> nal_unit);

}  // namespace amlogic_decoder

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_UTILS_H_
