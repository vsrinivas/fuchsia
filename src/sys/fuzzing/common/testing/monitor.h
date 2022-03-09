// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_TESTING_MONITOR_H_
#define SRC_SYS_FUZZING_COMMON_TESTING_MONITOR_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>

#include <deque>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/status.h"

namespace fuzzing {

using ::fuchsia::fuzzer::Monitor;
using ::fuchsia::fuzzer::UpdateReason;

// This is an implementation of |fuchsia.fuzzer.Monitor| for testing. It handles both the controller
// and user dispatch threads, and provides a way to await the next update.
class FakeMonitor final : public Monitor {
 public:
  explicit FakeMonitor(ExecutorPtr executor);
  ~FakeMonitor() override = default;

  bool is_bound() const { return binding_.is_bound(); }
  bool empty() const { return updates_.empty(); }
  UpdateReason reason() const { return updates_.front().reason; }
  const Status& status() const { return updates_.front().status; }
  Status take_status() { return std::move(updates_.front().status); }
  void pop_front() { updates_.pop_front(); }

  // FIDL-related methods.
  fidl::InterfaceHandle<Monitor> NewBinding();
  void Update(UpdateReason reason, Status status, UpdateCallback callback) override;

  // TODO(fxbug.dev/92490): There needs to be a way to wait on an update until the |Controller| is
  // fully updated to use the same executor as the test when testing.
  Promise<> AwaitUpdate();

 private:
  struct StatusUpdate {
    UpdateReason reason;
    Status status;
  };

  fidl::Binding<Monitor> binding_;
  ExecutorPtr executor_;
  std::deque<StatusUpdate> updates_;
  fpromise::suspended_task task_;
  Scope scope_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(FakeMonitor);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_TESTING_MONITOR_H_
