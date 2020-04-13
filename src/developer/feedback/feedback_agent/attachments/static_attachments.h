// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ATTACHMENTS_STATIC_ATTACHMENTS_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ATTACHMENTS_STATIC_ATTACHMENTS_H_

#include "src/developer/feedback/feedback_agent/attachments/aliases.h"

namespace feedback {

// Synchronously fetches the static attachments, i.e. the attachments that don't change during a
// boot cycle.
Attachments GetStaticAttachments(const AttachmentKeys& allowlist);

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ATTACHMENTS_STATIC_ATTACHMENTS_H_
