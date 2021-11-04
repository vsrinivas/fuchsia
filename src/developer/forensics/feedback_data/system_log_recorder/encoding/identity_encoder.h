// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_IDENTITY_ENCODER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_IDENTITY_ENCODER_H_

#include <string>

#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/encoder.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/version.h"

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {

class IdentityEncoder : public Encoder {
 public:
  IdentityEncoder(){}

  virtual ~IdentityEncoder(){}

  virtual EncodingVersion GetEncodingVersion() const { return EncodingVersion::kIdentity; }

  // |Encoder|
  virtual std::string Encode(const std::string& msg) { return msg; }
  virtual void Reset(){}
};

}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_IDENTITY_ENCODER_H_
