// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/lz4_decoder.h"

#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/lz4_utils.h"
#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {

const std::string kDecodingErrorStr = "!!! DECODING ERROR !!!\n";
const std::string kDecodingSizeErrorStr =
    "!!! CANNOT DECODE %lu BYTES. THERE ARE ONLY %lu BYTES LEFT !!!\n";

Lz4Decoder::Lz4Decoder() : ring_(kDecoderRingBufferSize) { stream_ = LZ4_createStreamDecode(); }

Lz4Decoder::~Lz4Decoder() { LZ4_freeStreamDecode(stream_); }

bool Lz4Decoder::DecodeNextChunk(const char* block_end, const char** block_ptr,
                                 std::string* decoded_chunk, std::string* err_msg) {
  *decoded_chunk = "";
  *err_msg = "";

  if (*block_ptr >= block_end) {
    return false;
  }

  const uint16_t encoded_bytes = DecodeSize(block_ptr);

  // This indicates that the encoder reset its stream because it became invalid. If so, we reset
  // the decoder too.
  if (encoded_bytes == kEncodeSizeError) {
    Reset();
    return true;
  }

  // Check for size error.
  if (*block_ptr + encoded_bytes > block_end) {
    size_t bytes_left = block_end - *block_ptr;
    *err_msg = fxl::StringPrintf(kDecodingSizeErrorStr.c_str(), encoded_bytes, bytes_left);
    return false;
  }

  const int decoded_bytes = LZ4_decompress_safe_continue(stream_, *block_ptr, ring_.GetPtr(),
                                                         encoded_bytes, kMaxChunkSize);
  // Check for decoding error.
  if (decoded_bytes < 0) {
    *err_msg = kDecodingErrorStr;
    return false;
  }

  *decoded_chunk = std::string(ring_.GetPtr(), decoded_bytes);

  // Update index variables.
  *block_ptr += encoded_bytes;
  ring_.Advance(decoded_bytes);

  return true;
}

std::string Lz4Decoder::DecodeWithoutReset(const std::string& chunks) {
  // initialize variables
  std::vector<std::string> decoded_data = {};
  const char* block_ptr = chunks.data();
  const char* block_end = chunks.data() + chunks.size();
  std::string decoded_chunk;
  std::string err_msg;

  // If err_msg is set, DecodeNextChunk() will return false and we will abort DecodeWithoutReset().
  while (DecodeNextChunk(block_end, &block_ptr, &decoded_chunk, &err_msg)) {
    decoded_data.push_back(decoded_chunk);
  }

  if (!err_msg.empty()) {
    decoded_data.push_back(err_msg);
  }

  return fxl::JoinStrings(decoded_data);
}

std::string Lz4Decoder::Decode(const std::string& block) {
  const std::string output = DecodeWithoutReset(block);
  Reset();
  return output;
}

void Lz4Decoder::Reset() {
  LZ4_freeStreamDecode(stream_);
  stream_ = LZ4_createStreamDecode();
  ring_.Reset();
}

}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics
