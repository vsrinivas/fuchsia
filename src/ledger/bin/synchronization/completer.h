// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_SYNCHRONIZATION_COMPLETER_H_
#define SRC_LEDGER_BIN_SYNCHRONIZATION_COMPLETER_H_

#include <lib/fit/function.h>

#include <vector>

#include "src/ledger/bin/public/status.h"
#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/lib/fxl/macros.h"

namespace ledger {

// TODO(opalle): Consider using DelayingFacade instead of Completer.
// A Completer allowing waiting until the target operation is completed.
class Completer {
 public:
  Completer();

  ~Completer();

  // Completes the operation with the given status and unblocks all pending
  // |WaitUntilDone| calls. |Complete| can only be called once.
  void Complete(Status status);

  // Blocks execution until |Complete| is called, and then returns its status.
  // If the operation is already completed, |WaitUntilDone| returns
  // immediately with the result status.
  void WaitUntilDone(fit::function<void(Status)> callback);

  // Returns true, if the operation was completed.
  bool IsCompleted();

 private:
  bool completed_ = false;
  Status status_;
  // Closures invoked upon completion to unblock the waiting coroutines.
  std::vector<fit::function<void(Status)>> callbacks_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Completer);
};

Status SyncWaitUntilDone(coroutine::CoroutineHandler* handler, Completer* completer);

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_SYNCHRONIZATION_COMPLETER_H_
