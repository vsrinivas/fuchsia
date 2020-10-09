// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_CONFIG_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_CONFIG_H_

#include <zircon/types.h>

#include <string>

#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/feedback_data/attachments/types.h"

namespace forensics {
namespace feedback_data {

// Feedback data provider configuration.
struct Config {
  // Set of annotation keys to return data for in fuchsia.feedback.DataProvider/GetSnapshot.
  AnnotationKeys annotation_allowlist;

  // Set of attachment keys to return data for in fuchsia.feedback.DataProvider/GetSnapshot.
  AttachmentKeys attachment_allowlist;
};

// Parses the JSON config at |filepath| as |config|.
zx_status_t ParseConfig(const std::string& filepath, Config* config);

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_CONFIG_H_
