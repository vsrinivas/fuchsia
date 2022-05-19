// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_ENCODE_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_ENCODE_H_

#include <fuchsia/feedback/cpp/fidl.h>

#include "src/developer/forensics/feedback/annotations/types.h"
#include "third_party/rapidjson/include/rapidjson/document.h"

namespace forensics::feedback {

// Returns |annotations| serialize as a certain type.
template <typename T>
T Encode(const Annotations& annotations);

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_ENCODE_H_
