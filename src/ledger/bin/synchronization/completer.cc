// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/synchronization/completer.h"

#include "src/ledger/lib/coroutine/coroutine.h"
#include "src/lib/callback/scoped_task_runner.h"
#include "src/lib/fxl/logging.h"

namespace ledger {

Completer::Completer(async_dispatcher_t* dispatcher) : task_runner_(dispatcher) {}

Completer::~Completer() = default;

void Completer::Complete(Status status) {
  FXL_DCHECK(!completed_);
  completed_ = true;
  status_ = status;
  // We need to move the callbacks in the stack since calling any of the
  // them might lead to the deletion of this object, invalidating callbacks_.
  std::vector<fit::function<void(Status)>> callbacks = std::move(callbacks_);
  callbacks_.clear();
  task_runner_.PostTask([status, callbacks = std::move(callbacks)] {
    for (const auto& callback : callbacks) {
      callback(status);
    }
  });
}

void Completer::WaitUntilDone(fit::function<void(Status)> callback) {
  if (completed_) {
    callback(status_);
  }

  callbacks_.push_back(std::move(callback));
}

bool Completer::IsCompleted() { return completed_; }

Status SyncWaitUntilDone(coroutine::CoroutineHandler* handler, Completer* completer) {
  Status status;
  if (coroutine::SyncCall(
          handler,
          [completer](fit::function<void(Status)> callback) {
            completer->WaitUntilDone(std::move(callback));
          },
          &status) == coroutine::ContinuationStatus::INTERRUPTED) {
    return Status::INTERRUPTED;
  }
  return status;
}
}  // namespace ledger
