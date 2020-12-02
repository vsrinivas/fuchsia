// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/lz4_encoder.h"

#include <lib/syslog/cpp/macros.h>

#include <iterator>

#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/lz4_utils.h"

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {

const std::string kDroppedError = "!!! DROPPED MESSAGE NOT ENCODABLE !!!\n";

Lz4Encoder::Lz4Encoder() : ring_(kEncoderRingBufferSize) { stream_ = LZ4_createStream(); }

Lz4Encoder::~Lz4Encoder() { LZ4_freeStream(stream_); }

std::string Lz4Encoder::Encode(const std::string& msg) {
  if (msg.size() == 0) {
    return "";
  }

  // lz4 forces us to output separately (1) the size of the encoded message and (2) the encoded
  // message itself.
  const size_t max_encoded_size = LZ4_compressBound((int)msg.size());
  std::vector<char> encoded(max_encoded_size);

  // Make a copy that will stay in memory for LZ4 to use
  char* chunk_copy_ptr = ring_.Write(msg.data(), msg.size());

  // Encode message.
  const int encoded_size = LZ4_compress_fast_continue(stream_, chunk_copy_ptr, encoded.data(),
                                                      (int)msg.size(), (int)max_encoded_size, 0);

  FX_CHECK((size_t)encoded_size <= kMaxChunkSize);

  if (encoded_size <= 0) {
    // We reset the encoder stream as its state has become undefined. Sending a special size
    // kEncodeSizeError is a proxy to reset the decoder to keep it in sync.
    Reset();
    return EncodeSize(kEncodeSizeError) + Encode(kDroppedError);
  }

  return EncodeSize((uint16_t)encoded_size) +
         std::string(encoded.begin(), encoded.begin() + encoded_size);
}

void Lz4Encoder::Reset() {
  LZ4_freeStream(stream_);
  stream_ = LZ4_createStream();
};

}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics
