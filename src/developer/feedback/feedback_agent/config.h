// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_CONFIG_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_CONFIG_H_

#include <zircon/types.h>

#include <string>

#include "src/developer/feedback/feedback_agent/annotations/aliases.h"
#include "src/developer/feedback/feedback_agent/attachments/aliases.h"

namespace feedback {

// Feedback data provider configuration.
struct Config {
  // Set of annotation keys to return data for in fuchsia.feedback.DataProvider.GetData().
  AnnotationKeys annotation_allowlist;

  // Set of attachment keys to return data for in fuchsia.feedback.DataProvider.GetData().
  AttachmentKeys attachment_allowlist;
};

// Parses the JSON config at |filepath| as |config|.
zx_status_t ParseConfig(const std::string& filepath, Config* config);

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_CONFIG_H_
