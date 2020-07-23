// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_LZ4_ENCODER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_LZ4_ENCODER_H_

#include <string>
#include <vector>

#include <lz4/lz4.h>

#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/encoder.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/ring_buffer.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/version.h"

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {

// Each call to Encode() returns a "chunk" - lz4 calls it a "block", but for us, a block is much
// larger and is made of many chunks. Reset() is called between blocks typically. A chunk is made
// of two consecutive parts: (1) the size of the lz4 encoded message and (2) the lz4 encoded
// message itself.
class Lz4Encoder : public Encoder {
 public:
  Lz4Encoder();

  virtual ~Lz4Encoder();

  virtual EncodingVersion GetEncodingVersion() const { return EncodingVersion::kLz4; }

  // |Encoder|
  virtual std::string Encode(const std::string& data);
  virtual void Reset();

 private:
  LZ4_stream_t* stream_;
  RingBuffer ring_;
};

}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_LZ4_ENCODER_H_
