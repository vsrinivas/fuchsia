// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_TESTS_INTEGRATION_H264_UTILS_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_TESTS_INTEGRATION_H264_UTILS_H_

#include <cstdint>
#include <vector>

std::vector<std::vector<uint8_t>> SplitNalUnits(const uint8_t* start_data, uint32_t size);
uint8_t GetNalUnitType(const std::vector<uint8_t>& nal_unit);

#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_TESTS_INTEGRATION_H264_UTILS_H_
