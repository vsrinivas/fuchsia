// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_PRODUCTION_ENCODING_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_PRODUCTION_ENCODING_H_

#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/lz4_decoder.h"
#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/lz4_encoder.h"

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {

// Set the encoder - decoder pair for encoding log messages in production.
using ProductionDecoder = Lz4Decoder;
using ProductionEncoder = Lz4Encoder;

}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_PRODUCTION_ENCODING_H_
