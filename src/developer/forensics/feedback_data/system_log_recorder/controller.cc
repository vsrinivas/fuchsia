// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/system_log_recorder/controller.h"

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {

Controller::Controller(async::Loop* main_loop, async::Loop* write_loop,
                       SystemLogRecorder* system_log_recorder)
    : main_loop_(main_loop), write_loop_(write_loop), system_log_recorder_(system_log_recorder) {}

void Controller::SetStop(::fit::closure stop) { stop_ = std::move(stop); }

void Controller::DisableAndDropPersistentLogs(DisableAndDropPersistentLogsCallback callback) {
  system_log_recorder_->StopAndDeleteLogs();
  callback();
  write_loop_->Shutdown();
  main_loop_->Shutdown();
}

void Controller::Stop() { stop_(); }

}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics
