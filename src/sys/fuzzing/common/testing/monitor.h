// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_TESTING_MONITOR_H_
#define SRC_SYS_FUZZING_COMMON_TESTING_MONITOR_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/sync/completion.h>

#include <deque>
#include <memory>
#include <mutex>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/dispatcher.h"
#include "src/sys/fuzzing/common/testing/binding.h"

namespace fuzzing {

using ::fuchsia::fuzzer::Monitor;
using ::fuchsia::fuzzer::MonitorPtr;
using ::fuchsia::fuzzer::Status;
using ::fuchsia::fuzzer::UpdateReason;

// This is an implementation of |fuchsia.fuzzer.Monitor| for testing. It handles both the controller
// and user dispatch threads, and provides a way to await the next update.
class FakeMonitor final : public Monitor {
 public:
  FakeMonitor();
  ~FakeMonitor() override = default;

  // FIDL-related methods.
  fidl::InterfaceHandle<Monitor> NewBinding();
  MonitorPtr Bind(const std::shared_ptr<Dispatcher>& dispatcher);
  void Update(UpdateReason reason, Status status, UpdateCallback callback) override;

  // Blocks until the next call to |Update| and returns the provided reason.
  UpdateReason NextReason();

  // Blocks until the next call to |Update| and returns the provided status, and |reason| if not
  // null.
  Status NextStatus(UpdateReason* out_reason = nullptr);

 private:
  FakeBinding<Monitor> binding_;
  std::mutex mutex_;
  std::deque<UpdateReason> reasons_;
  std::deque<Status> statuses_;
  sync_completion_t sync_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(FakeMonitor);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_TESTING_MONITOR_H_
