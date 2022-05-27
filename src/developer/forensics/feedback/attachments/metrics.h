// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_ATTACHMENTS_METRICS_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_ATTACHMENTS_METRICS_H_

#include "src/developer/forensics/feedback/attachments/types.h"
#include "src/developer/forensics/utils/cobalt/logger.h"

namespace forensics::feedback {

class AttachmentMetrics {
 public:
  explicit AttachmentMetrics(cobalt::Logger* cobalt);

  // Sends metrics related to |attachments| to Cobalt.
  void LogMetrics(const Attachments& attachments);

 private:
  cobalt::Logger* cobalt_;
};

}  // namespace forensics::feedback

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_ATTACHMENTS_METRICS_H_
