// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_VP9_UTILS_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_VP9_UTILS_H_

#include <cstdint>
#include <vector>

std::vector<uint32_t> TryParseSuperframeHeader(const uint8_t* data, uint32_t frame_size);
void SplitSuperframe(const uint8_t* data, uint32_t frame_size, std::vector<uint8_t>* output_vector,
                     std::vector<uint32_t>* superframe_byte_sizes = nullptr);

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_VP9_UTILS_H_
