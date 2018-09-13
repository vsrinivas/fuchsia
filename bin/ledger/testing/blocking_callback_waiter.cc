// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/blocking_callback_waiter.h"

#include <lib/fit/defer.h>

namespace ledger {

BlockingCallbackWaiter::BlockingCallbackWaiter(LoopController* loop_controller)
    : loop_controller_(loop_controller) {}

BlockingCallbackWaiter::~BlockingCallbackWaiter() {}

fit::function<void()> BlockingCallbackWaiter::GetCallback() {
  return [this] {
    ++callback_called_;
    if (waiting_) {
      loop_controller_->StopLoop();
    }
  };
}

bool BlockingCallbackWaiter::RunUntilCalled() {
  FXL_DCHECK(!waiting_);
  waiting_ = true;
  auto cleanup = fit::defer([this] { waiting_ = false; });
  while (NotCalledYet()) {
    loop_controller_->RunLoop();
  }
  ++run_until_called_;
  return true;
}

bool BlockingCallbackWaiter::NotCalledYet() {
  return callback_called_ <= run_until_called_;
}

}  // namespace ledger
