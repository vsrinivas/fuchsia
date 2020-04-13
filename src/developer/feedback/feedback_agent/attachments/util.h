// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ATTACHMENTS_UTIL_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ATTACHMENTS_UTIL_H_

#include <fuchsia/feedback/cpp/fidl.h>

#include <vector>

namespace feedback {

// Adds the |annotations| as an extra JSON attachment to |attachments|.
void AddAnnotationsAsExtraAttachment(const std::vector<fuchsia::feedback::Annotation>& annotations,
                                     std::vector<fuchsia::feedback::Attachment>* attachments);

// Bundles the |attachments| into a single attachment.
bool BundleAttachments(const std::vector<fuchsia::feedback::Attachment>& attachments,
                       fuchsia::feedback::Attachment* bundle);

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ATTACHMENTS_UTIL_H_
