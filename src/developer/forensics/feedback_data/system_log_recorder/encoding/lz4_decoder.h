// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_LZ4_DECODER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_LZ4_DECODER_H_

#include <string>

#include <lz4/lz4.h>

#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/decoder.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/ring_buffer.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/version.h"

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {

// The Lz4Decoder::Decode() decodes a block previously encoded with the Lz4Encoder. The block is
// processed one chunk at a time as required by LZ4. One chunk is created on every invocation to
// LZ4_compress_fast_continue(). The decoding algorithm further requires that the previous 64KB of
// decoded data remain in memory (unchanged) thus a ring buffer is used for this purpose. The ring
// buffer wraps around when there is not enough data left and we guarantee that there is at least
// 64KB of previous decoded data (it is very likely that the decoded data will be larger than 64KB
// if the encoded block size is 64KB and the compression ratio is greater than 1x). In addition,
// the state for the current block decompression needed by the LZ4 algorithm is kept in the
// “stream” variable.
class Lz4Decoder : public Decoder {
 public:
  Lz4Decoder();

  virtual ~Lz4Decoder();

  virtual EncodingVersion GetEncodingVersion() const { return EncodingVersion::kLz4; }

  // |Decoder|
  virtual std::string Decode(const std::string& block);

 protected:
  // Increases testing flow control.
  //
  // Decoding a block automatically resets the decoder. For testing however it is useful to decode
  // every message. This is because decoding large blocks can spam the test logs with tens of
  // thousands of characters and finding when or how a test fails becomes needlessly onerous.
  // Breaking a large block into smaller blocks also decreases the probability of finding errors
  // since the encoder, the decoder and the buffers get reset on every block.
  std::string DecodeWithoutReset(const std::string& chunk);

  void Reset();

 private:
  // Decodes the next chunk in the block. Returns whether the block should be further decoded.
  //
  // A chunk is made of two consecutive parts: (1) the size of the encoded message and (2) the
  // encoded message itself. block_ptr points to the start of the chunk (to be decoded) while
  // block_end points to 1-byte past the last element of the block (analogous to std::vector::end)
  bool DecodeNextChunk(const char* block_end, const char** block_ptr, std::string* decoded_chunk,
                       std::string* err_msg);

  LZ4_streamDecode_t* stream_;

  RingBuffer ring_;
};

}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_LZ4_DECODER_H_
