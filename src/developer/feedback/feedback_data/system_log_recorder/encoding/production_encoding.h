// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_PRODUCTION_ENCODING_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_PRODUCTION_ENCODING_H_

#include "src/developer/feedback/feedback_data/system_log_recorder/encoding/identity_decoder.h"
#include "src/developer/feedback/feedback_data/system_log_recorder/encoding/identity_encoder.h"

namespace feedback {

// Set the encoder - decoder pair for encoding log messages in production.
using ProductionDecoder = IdentityDecoder;
using ProductionEncoder = IdentityEncoder;

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_PRODUCTION_ENCODING_H_
