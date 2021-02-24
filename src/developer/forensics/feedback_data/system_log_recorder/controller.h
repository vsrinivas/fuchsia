// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_CONTROLLER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_CONTROLLER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/process/lifecycle/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>

#include "src/developer/forensics/feedback_data/system_log_recorder/system_log_recorder.h"

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {

class Controller : public fuchsia::feedback::DataProviderController,
                   public fuchsia::process::lifecycle::Lifecycle {
 public:
  Controller(async::Loop* main_loop, async::Loop* write_loop,
             SystemLogRecorder* system_log_recorder);

  void SetStop(::fit::closure stop);

  // Deletes any persisted logs, stops the system log recorder, and stops the component.
  //
  // |fuchsia.feedback.DataProviderController|
  void DisableAndDropPersistentLogs(DisableAndDropPersistentLogsCallback callback) override;

  // Immediately flushes the cached logs to disk.
  //
  // |fuchsia.process.lifecycle.Lifecycle|
  void Stop() override;

 private:
  async::Loop* main_loop_;
  async::Loop* write_loop_;
  SystemLogRecorder* system_log_recorder_;

  ::fit::closure stop_;
};

}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_CONTROLLER_H_
