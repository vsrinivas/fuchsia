// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/attachments/previous_system_log_ptr.h"

#include <lib/fit/promise.h>

#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/lib/files/file.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

fit::promise<AttachmentValue> CollectPreviousSystemLog() {
  std::string content;
  if (!files::ReadFileToString(kPreviousLogsFilePath, &content)) {
    FX_LOGS(ERROR) << "Unable to load previous logs into string";
    return fit::make_result_promise<AttachmentValue>(fit::error());
  }

  return fit::make_result_promise<AttachmentValue>(fit::ok(content));
}

}  // namespace feedback
