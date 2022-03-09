// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_MONITOR_CLIENTS_H_
#define SRC_SYS_FUZZING_COMMON_MONITOR_CLIENTS_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_ptr_set.h>

#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/status.h"

namespace fuzzing {

using ::fuchsia::fuzzer::Monitor;
using ::fuchsia::fuzzer::Status;
using ::fuchsia::fuzzer::UpdateReason;

// This class  encapsulates the pool of |fuchsia.fuzzer.Monitor| connections managed by the runner.
class MonitorClients final {
 public:
  explicit MonitorClients(ExecutorPtr executor);
  ~MonitorClients() = default;

  Status status() const { return CopyStatus(status_); }
  void set_status(Status&& status) { status_ = std::move(status); }

  // Adds a subscriber for status updates.
  void Add(fidl::InterfaceHandle<Monitor> monitor);

  // Collects the current status, labels it with the given |reason|, and schedules a promise to send
  // it to all the attached |Monitor|s. Multiple calls to |Update| are guaraneteed to be performed
  // in sequence. If |reason| is |DONE|, this will |CloseAll| connections on completion of the
  // scheduled promise.
  void Update(UpdateReason reason);

  // Returns a promise that waits for a previous |Update| to be acknowledged by the monitors. This
  // is mostly useful when testing; in normal operation |Update|s can be treated as "fire and
  // forget".
  Promise<> AwaitAcknowledgement();

  // Closes all monitor connections.
  void CloseAll();

 private:
  ExecutorPtr executor_;
  Status status_;
  fidl::InterfacePtrSet<Monitor> monitors_;
  Consumer<> previous_;
  Scope scope_;
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_MONITOR_CLIENTS_H_
