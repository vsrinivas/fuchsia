// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_DECODE_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_DECODE_H_

#include <fuchsia/feedback/cpp/fidl.h>

#include <optional>

#include "src/developer/forensics/feedback/annotations/types.h"

namespace forensics::feedback {

// Deserializes |annotations| as Annotations.
Annotations FromFidl(const std::vector<fuchsia::feedback::Annotation>& annotations);

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ANNOTATIONS_DECODE_H_
