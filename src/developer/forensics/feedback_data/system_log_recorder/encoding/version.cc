// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/version.h"

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {

cobalt::PreviousBootEncodingVersion ToCobalt(EncodingVersion version) {
  switch (version) {
    case EncodingVersion::kForTesting:
      return cobalt::PreviousBootEncodingVersion::kUnknown;
    case EncodingVersion::kIdentity:
      return cobalt::PreviousBootEncodingVersion::kV_01;
    case EncodingVersion::kLz4:
      return cobalt::PreviousBootEncodingVersion::kV_02;
  }
}

}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics
