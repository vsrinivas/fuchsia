// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_LZ4_UTILS_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_LZ4_UTILS_H_

#include <cstdint>
#include <string>

#include <lz4/lz4.h>

#include "src/developer/forensics/feedback_data/constants.h"

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {

constexpr size_t kMaxChunkSize = ::forensics::feedback_data::kMaxWriteSizeInBytes;
static_assert(LZ4_COMPRESSBOUND(kMaxChunkSize) > 0, "The chunk size is invalid!");

// Due to EncodeSize() limitations, enforce that the encode size fits in 2 bytes.
static_assert(LZ4_COMPRESSBOUND(kMaxChunkSize) < UINT16_MAX,
              "The encoded chunk size could not fit in 2 bytes!");

static_assert(kMaxChunkSize < 64 * 1024,
              "Lz4 utilizes the last 64KB for its algorithm; there is little to no gain for making "
              "a chunk > 64KB.");

constexpr size_t kDecoderRingBufferSize = LZ4_DECODER_RING_BUFFER_SIZE(kMaxChunkSize);

// This indicates to the decoder that the encoder reset so it should reset as well.
constexpr size_t kEncodeSizeError = 0;

// The encoder needs additional kMaxChunkSize space because we replace the contents before calling
// the LZ4 encoder.
constexpr size_t kEncoderRingBufferSize = kDecoderRingBufferSize + kMaxChunkSize;

// Encodes the size of the encoded chunk as a fixed-length string that is easily decodable. This
// must be kept in sync with DecodeSize().
//
// The inline keyword avoids breaking the one definition rule.
inline std::string EncodeSize(uint16_t size) {
  std::string num(2, '\0');
  num[0] = (size >> 8);
  num[1] = (size & 0x00FF);
  return num;
}

// Reads the uint16_t value from the first two consecutive characters starting at the *data_ptr
// position and updates the *data_ptr position in order to read subsequent data.
//
// The inline keyword avoids breaking the one definition rule.
//
// Note: we do not cast directly into uint16_t to avoid alignment issues.
inline uint16_t DecodeSize(const char** data_ptr) {
  uint16_t size = (uint16_t)((unsigned char)(*data_ptr)[0] << 8) + (unsigned char)(*data_ptr)[1];
  *data_ptr += 2;
  return size;
}

}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_LZ4_UTILS_H_
