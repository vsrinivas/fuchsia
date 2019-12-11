// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/loop_controller_test_loop.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/defer.h>

#include <memory>

#include "src/ledger/lib/logging/logging.h"

namespace ledger {

namespace {

class SubLoopTestLoop : public SubLoop {
 public:
  explicit SubLoopTestLoop(LoopController* controller,
                           std::unique_ptr<async::LoopInterface> loop_interface)
      : controller_(controller), loop_interface_(std::move(loop_interface)) {}

  void DrainAndQuit() override {
    // TODO(qsr): Implement drain on TestLoop.
    auto waiter = controller_->NewWaiter();
    async::PostTask(dispatcher(), waiter->GetCallback());
    LEDGER_CHECK(waiter->RunUntilCalled());
  }

  async_dispatcher_t* dispatcher() override { return loop_interface_->dispatcher(); }

 private:
  LoopController* controller_;
  std::unique_ptr<async::LoopInterface> loop_interface_;
};

class CallbackWaiterImpl : public CallbackWaiter {
 public:
  explicit CallbackWaiterImpl(LoopController* loop) : loop_(loop) {}
  CallbackWaiterImpl(const CallbackWaiterImpl&) = delete;
  CallbackWaiterImpl& operator=(const CallbackWaiterImpl&) = delete;
  ~CallbackWaiterImpl() override = default;

  fit::function<void()> GetCallback() override {
    return [this] {
      ++callback_called_count_;
      if (running_) {
        loop_->StopLoop();
      }
    };
  }

  bool RunUntilCalled() override {
    LEDGER_DCHECK(!running_);
    running_ = true;
    auto cleanup = fit::defer([this] { running_ = false; });
    bool called = loop_->RunLoopUntil([this] { return !NotCalledYet(); });
    if (called) {
      ++run_until_called_count_;
    }
    return called;
  }

  bool NotCalledYet() override { return callback_called_count_ <= run_until_called_count_; }

 private:
  LoopController* loop_;
  size_t callback_called_count_ = 0;
  size_t run_until_called_count_ = 0;
  // Whether the waiter is currently in the |RunUntilCalled| method.
  bool running_ = false;
};

}  // namespace

LoopControllerTestLoop::LoopControllerTestLoop(async::TestLoop* loop) : loop_(loop) {}

LoopControllerTestLoop::~LoopControllerTestLoop() = default;

void LoopControllerTestLoop::RunLoop() { loop_->RunUntilIdle(); }

void LoopControllerTestLoop::StopLoop() { loop_->Quit(); }

std::unique_ptr<SubLoop> LoopControllerTestLoop::StartNewLoop() {
  return std::make_unique<SubLoopTestLoop>(
      this, std::unique_ptr<async::LoopInterface>(loop_->StartNewLoop().release()));
}

std::unique_ptr<CallbackWaiter> LoopControllerTestLoop::NewWaiter() {
  return std::make_unique<CallbackWaiterImpl>(this);
}

async_dispatcher_t* LoopControllerTestLoop::dispatcher() { return loop_->dispatcher(); }

bool LoopControllerTestLoop::RunLoopUntil(fit::function<bool()> condition) {
  if (condition()) {
    return true;
  }
  // The condition is not true, but might be triggered after some delay due to a
  // delayed task (for example, because of backoffs). Try to advance the loop in
  // bigger and bigger increment. Fail if the event does not occur after ~100s
  // as if something doesn't happen in 100 simulated s, then it will almost
  // certainly be a problem for tests using a real loop.
  for (size_t i : {0, 1, 10, 100}) {
    loop_->RunFor(zx::sec(i));
    if (condition()) {
      return true;
    }
  }
  return false;
}

void LoopControllerTestLoop::RunLoopFor(zx::duration duration) { loop_->RunFor(duration); }

}  // namespace ledger
