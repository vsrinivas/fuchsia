// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_AGENT_ATTACHMENTS_H_
#define SRC_DEVELOPER_FEEDBACK_AGENT_ATTACHMENTS_H_

#include <vector>

#include <fuchsia/feedback/cpp/fidl.h>

namespace fuchsia {
namespace feedback {

// Returns attachments useful to attach in feedback reports (crash or user
// feedback).
std::vector<Attachment> GetAttachments();

}  // namespace feedback
}  // namespace fuchsia

#endif  // SRC_DEVELOPER_FEEDBACK_AGENT_ATTACHMENTS_H_
