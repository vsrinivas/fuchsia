// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/blocking_callback_waiter.h"

#include <lib/fit/defer.h>

#include "src/ledger/lib/logging/logging.h"

namespace ledger {

BlockingCallbackWaiter::BlockingCallbackWaiter(LoopController* loop_controller)
    : loop_controller_(loop_controller) {}

BlockingCallbackWaiter::~BlockingCallbackWaiter() = default;

fit::function<void()> BlockingCallbackWaiter::GetCallback() {
  live_callbacks_count_++;
  auto on_callback_deletion = fit::defer([this] {
    live_callbacks_count_--;
    if (live_callbacks_count_ == 0 && running_) {
      // All callbacks have went out of scope while |RunUntilCalled|: no other
      // callback can stop the loop, exit immediately.
      loop_controller_->StopLoop();
    }
  });
  return [this, on_callback_deletion = std::move(on_callback_deletion)] {
    ++callback_called_count_;
    if (running_) {
      // Called during |RunUntilCalled|: exit from the loop.
      loop_controller_->StopLoop();
    }
  };
}

bool BlockingCallbackWaiter::RunUntilCalled() {
  LEDGER_DCHECK(!running_);
  running_ = true;
  auto cleanup = fit::defer([this] { running_ = false; });
  while (NotCalledYet()) {
    if (live_callbacks_count_ == 0) {
      // Do not start the loop if no callback is available to stop it.
      return false;
    }
    loop_controller_->RunLoop();
  }
  ++run_until_called_count_;
  return true;
}

bool BlockingCallbackWaiter::NotCalledYet() {
  return callback_called_count_ <= run_until_called_count_;
}

}  // namespace ledger
