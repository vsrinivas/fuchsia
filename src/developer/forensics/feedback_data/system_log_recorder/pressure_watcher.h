// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_PRESSURE_WATCHER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_PRESSURE_WATCHER_H_

#include <fuchsia/memorypressure/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>

namespace forensics::feedback_data::system_log_recorder {

// Observes the system's memory pressure signal and executes a callback each time it changes.
//
// fuchsia.memorypressure.Watcher is expected to be in |services|.
class PressureWatcher {
 public:
  using OnLevelChangedFn = ::fit::function<void(fuchsia::memorypressure::Level)>;

  PressureWatcher(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services,
                  OnLevelChangedFn on_level_changed);

 private:
  void Connect();
  void OnError(zx_status_t status);

  async_dispatcher_t* dispatcher_;
  std::shared_ptr<sys::ServiceDirectory> services_;
  std::unique_ptr<fuchsia::memorypressure::Watcher> watcher_;
  ::fidl::Binding<fuchsia::memorypressure::Watcher> connection_;
};

}  // namespace forensics::feedback_data::system_log_recorder

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_PRESSURE_WATCHER_H_
