// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_AGENT_ANNOTATIONS_H_
#define SRC_DEVELOPER_FEEDBACK_AGENT_ANNOTATIONS_H_

#include <fuchsia/feedback/cpp/fidl.h>

#include <vector>

namespace fuchsia {
namespace feedback {

// Returns annotations useful to attach in feedback reports (crash or user
// feedback).
std::vector<Annotation> GetAnnotations();

}  // namespace feedback
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_FEEDBACK_AGENT_ANNOTATIONS_H_
