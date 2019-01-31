// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTING_BLOCKING_CALLBACK_WAITER_H_
#define PERIDOT_BIN_LEDGER_TESTING_BLOCKING_CALLBACK_WAITER_H_

#include "peridot/bin/ledger/testing/loop_controller.h"

namespace ledger {

// An implementation of |CallbackWaiter| that will block indefinitely until its
// callback is called.
class BlockingCallbackWaiter : public CallbackWaiter {
 public:
  explicit BlockingCallbackWaiter(LoopController* loop_controller);
  BlockingCallbackWaiter(const BlockingCallbackWaiter&) = delete;
  BlockingCallbackWaiter& operator=(const BlockingCallbackWaiter&) = delete;
  ~BlockingCallbackWaiter() override;

  fit::function<void()> GetCallback() override;
  bool RunUntilCalled() override;
  bool NotCalledYet() override;

 private:
  LoopController* loop_controller_;
  size_t callback_called_count_ = 0;
  size_t run_until_called_count_ = 0;
  size_t live_callbacks_count_ = 0;
  // Whether the waiter is currently in the |RunUntilCalled| method.
  bool running_ = false;
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_TESTING_BLOCKING_CALLBACK_WAITER_H_
