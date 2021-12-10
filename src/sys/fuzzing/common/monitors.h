// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_MONITORS_H_
#define SRC_SYS_FUZZING_COMMON_MONITORS_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_ptr_set.h>

#include <mutex>
#include <vector>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/sys/fuzzing/common/dispatcher.h"
#include "src/sys/fuzzing/common/sync-wait.h"

namespace fuzzing {

using ::fuchsia::fuzzer::Monitor;
using ::fuchsia::fuzzer::MonitorPtr;
using ::fuchsia::fuzzer::Status;
using ::fuchsia::fuzzer::UpdateReason;

// This class  encapsulates the pool of |fuchsia.fuzzer.Monitor| connections managed by the runner.
class MonitorClients final {
 public:
  MonitorClients();
  ~MonitorClients();

  // Adds a subscriber for status updates.
  void Add(fidl::InterfaceHandle<Monitor> monitor);

  Status GetStatus() FXL_LOCKS_EXCLUDED(mutex_);
  void SetStatus(Status status) FXL_LOCKS_EXCLUDED(mutex_);

  // Collects the current status, labels it with the given |reason|, and sends it to all the
  // attached |Monitor|s.
  void Update(UpdateReason reason) FXL_LOCKS_EXCLUDED(mutex_);

 private:
  // Like |Update|, but uses UpdateReason::DONE as the reason and disconnects monitors after
  // they acknowledge receipt.
  void Finish() FXL_LOCKS_EXCLUDED(mutex_);

  // Closes all monitor connections. Much like |Binding::Unbind|, this may be called from any
  // thread, not just the FIDL dispatcher thread.
  void CloseAll();

  Dispatcher dispatcher_;
  std::mutex mutex_;
  Status status_ FXL_GUARDED_BY(mutex_);

  // This is only ever accessed from the dispatcher thread.
  fidl::InterfacePtrSet<Monitor> monitors_;

  // Blocks calls to |Add| if a call to |Finish| is in progress until the latter completes.
  SyncWait allow_add_;
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_MONITORS_H_
