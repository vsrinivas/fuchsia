// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ATTACHMENTS_ALIASES_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ATTACHMENTS_ALIASES_H_

#include <map>
#include <set>
#include <string>

namespace feedback {

using AttachmentKey = std::string;
using AttachmentKeys = std::set<AttachmentKey>;

using AttachmentValue = std::string;

using Attachment = std::pair<AttachmentKey, AttachmentValue>;
using Attachments = std::map<AttachmentKey, AttachmentValue>;

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_ATTACHMENTS_ALIASES_H_
