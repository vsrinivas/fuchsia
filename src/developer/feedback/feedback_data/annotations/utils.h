// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_DATA_ANNOTATIONS_UTILS_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_DATA_ANNOTATIONS_UTILS_H_

#include "src/developer/feedback/feedback_data/annotations/aliases.h"

namespace feedback {

AnnotationKeys RestrictAllowlist(const AnnotationKeys& allowlist,
                                 const AnnotationKeys& restrict_to);

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_DATA_ANNOTATIONS_UTILS_H_
