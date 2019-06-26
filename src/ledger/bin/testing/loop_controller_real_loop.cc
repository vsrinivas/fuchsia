// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/loop_controller_real_loop.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/zx/time.h>

#include "src/ledger/bin/testing/blocking_callback_waiter.h"

namespace ledger {

namespace {

// Implementation of a SubLoop that uses a real loop.
class SubLoopRealLoop : public SubLoop {
 public:
  SubLoopRealLoop() : loop_(&kAsyncLoopConfigNoAttachToThread) { loop_.StartThread(); };

  ~SubLoopRealLoop() override { loop_.Shutdown(); }

  void DrainAndQuit() override {
    async::TaskClosure quit_task([this] { loop_.Quit(); });
    quit_task.Post(loop_.dispatcher());
    loop_.JoinThreads();
  }

  async_dispatcher_t* dispatcher() override { return loop_.dispatcher(); }

 private:
  async::Loop loop_;
};

}  // namespace

LoopControllerRealLoop::LoopControllerRealLoop() : loop_(&kAsyncLoopConfigAttachToThread) {}

LoopControllerRealLoop::~LoopControllerRealLoop() {}

void LoopControllerRealLoop::RunLoop() {
  loop_.Run();
  loop_.ResetQuit();
}

void LoopControllerRealLoop::StopLoop() { loop_.Quit(); }

std::unique_ptr<SubLoop> LoopControllerRealLoop::StartNewLoop() {
  return std::make_unique<SubLoopRealLoop>();
}

std::unique_ptr<CallbackWaiter> LoopControllerRealLoop::NewWaiter() {
  return std::make_unique<BlockingCallbackWaiter>(this);
}

async_dispatcher_t* LoopControllerRealLoop::dispatcher() { return loop_.dispatcher(); }

bool LoopControllerRealLoop::RunLoopUntil(fit::function<bool()> condition) {
  while (true) {
    if (condition()) {
      return true;
    }
    RunLoopFor(zx::msec(10));
  }
}

void LoopControllerRealLoop::RunLoopFor(zx::duration duration) {
  async::TaskClosure task([this] { loop_.Quit(); });
  task.PostDelayed(loop_.dispatcher(), duration);
  loop_.Run();
  loop_.ResetQuit();
}

}  // namespace ledger
