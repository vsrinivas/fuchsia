// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/loop_controller.h"

#include <lib/fxl/memory/ref_ptr.h>
#include <lib/zx/time.h>

namespace ledger {
namespace {
class CallbackWaiterImpl : public CallbackWaiter {
 public:
  explicit CallbackWaiterImpl(LoopController* loop_controller)
      : loop_controller_(loop_controller) {}
  CallbackWaiterImpl(const CallbackWaiterImpl&) = delete;
  CallbackWaiterImpl& operator=(const CallbackWaiterImpl&) = delete;
  ~CallbackWaiterImpl() override = default;

  fit::function<void()> GetCallback() override {
    return [this] {
      ++callback_called_;
      if (waiting_) {
        loop_controller_->StopLoop();
      }
    };
  }

  bool RunUntilCalled() override {
    FXL_DCHECK(!waiting_);
    waiting_ = true;
    while (NotCalledYet()) {
      loop_controller_->RunLoop();
    }
    waiting_ = false;
    ++run_until_called_;
    return true;
  }

  bool NotCalledYet() override { return callback_called_ <= run_until_called_; }

 private:
  LoopController* loop_controller_;
  size_t callback_called_ = 0;
  size_t run_until_called_ = 0;
  bool waiting_ = false;
};
}  // namespace

std::unique_ptr<CallbackWaiter> LoopController::NewWaiter() {
  return std::make_unique<CallbackWaiterImpl>(this);
}

}  // namespace ledger
