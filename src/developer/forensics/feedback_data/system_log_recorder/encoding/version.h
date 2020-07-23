// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_VERSION_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_VERSION_H_

#include "src/developer/forensics/utils/cobalt/metrics.h"

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {

enum class EncodingVersion {
  kForTesting,
  kIdentity,
  kLz4,
};

cobalt::PreviousBootEncodingVersion ToCobalt(EncodingVersion version);

}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_VERSION_H_
