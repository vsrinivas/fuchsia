// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/crashpad_agent/info/queue_info.h"

#include "src/lib/fxl/logging.h"

namespace feedback {

QueueInfo::QueueInfo(std::shared_ptr<InfoContext> context) : context_(context) {
  FXL_CHECK(context);
}

void QueueInfo::LogReport(const std::string& program_name, const std::string& local_report_id) {
  context_->InspectManager().AddReport(program_name, local_report_id);
}

void QueueInfo::SetSize(const uint64_t size) { context_->InspectManager().SetQueueSize(size); }

}  // namespace feedback
