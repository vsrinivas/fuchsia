// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_DECODER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_DECODER_H_

#include <string>

#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/version.h"

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {

// Decodes data previously encoded via an Encoder.
//
// As an Encoder operates on a block, the Decoder needs to decode a whole block at once.
class Decoder {
 public:
  virtual ~Decoder(){}

  virtual EncodingVersion GetEncodingVersion() const = 0;

  virtual std::string Decode(const std::string& block) = 0;
};

}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_DECODER_H_
