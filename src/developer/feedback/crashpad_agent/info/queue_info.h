// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_QUEUE_INFO_H_
#define SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_QUEUE_INFO_H_

#include <memory>

#include "src/developer/feedback/crashpad_agent/info/info_context.h"
#include "src/lib/fxl/logging.h"

namespace feedback {

// Information about the queue we want to export.
struct QueueInfo {
 public:
  QueueInfo(std::shared_ptr<InfoContext> context);

  void LogReport(const std::string& program_name, const std::string& local_report_id);

 private:
  std::shared_ptr<InfoContext> context_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_CRASHPAD_AGENT_QUEUE_INFO_H_
