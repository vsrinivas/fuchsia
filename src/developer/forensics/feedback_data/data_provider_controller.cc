// Copyright 2021 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/data_provider_controller.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/forensics/feedback_data/constants.h"
#include "src/lib/files/file.h"

namespace forensics {
namespace feedback_data {

void DataProviderController::BindSystemLogRecorderController(zx::channel channel,
                                                             async_dispatcher_t* dispatcher) {
  if (const auto status = system_log_recorder_controller_.Bind(std::move(channel), dispatcher);
      status != ZX_OK) {
    FX_LOGS(FATAL) << "Failed to bind to the system log recorder controller";
  }
}

void DataProviderController::DisableAndDropPersistentLogs(
    DisableAndDropPersistentLogsCallback callback) {
  files::WriteFile(kDoNotLaunchSystemLogRecorder, "");

  if (system_log_recorder_controller_.is_bound()) {
    system_log_recorder_controller_->DisableAndDropPersistentLogs([callback = std::move(callback)] {
      FX_LOGS(INFO) << "Persistent logging has been disabled";
      callback();
    });
  } else {
    callback();
  }
}

}  // namespace feedback_data
}  // namespace forensics
