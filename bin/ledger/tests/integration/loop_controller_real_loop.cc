// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/tests/integration/loop_controller_real_loop.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/zx/time.h>

namespace {

// Returns false if the loop was ran for less than |timeout|.
bool RunGivenLoopWithTimeout(async::Loop* loop, zx::duration timeout) {
  // This cannot be a local variable because the delayed task below can execute
  // after this function returns.
  auto canceled = std::make_shared<bool>(false);
  bool timed_out = false;
  async::PostDelayedTask(loop->dispatcher(),
                         [loop, canceled, &timed_out] {
                           if (*canceled) {
                             return;
                           }
                           timed_out = true;
                           loop->Quit();
                         },
                         timeout);
  loop->Run();
  loop->ResetQuit();
  // Another task can call Quit() on the message loop, which exits the
  // message loop before the delayed task executes, in which case |timed_out| is
  // still false here because the delayed task hasn't run yet.
  // Since the message loop isn't destroyed then (as it usually would after
  // Quit()), and presumably can be reused after this function returns we
  // still need to prevent the delayed task to quit it again at some later time
  // using the canceled pointer.
  if (!timed_out) {
    *canceled = true;
  }
  return timed_out;
}

}  // namespace

namespace ledger {

// Implementation of a SubLoop that uses a real loop.
class SubLoopRealLoop : public SubLoop {
 public:
  SubLoopRealLoop() : loop_(&kAsyncLoopConfigNoAttachToThread) {
    loop_.StartThread();
  };

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

LoopControllerRealLoop::LoopControllerRealLoop()
    : loop_(&kAsyncLoopConfigAttachToThread) {}

void LoopControllerRealLoop::RunLoop() {
  loop_.Run();
  loop_.ResetQuit();
}

void LoopControllerRealLoop::StopLoop() { loop_.Quit(); }

std::unique_ptr<SubLoop> LoopControllerRealLoop::StartNewLoop() {
  return std::make_unique<SubLoopRealLoop>();
}

async_dispatcher_t* LoopControllerRealLoop::dispatcher() {
  return loop_.dispatcher();
}

fit::closure LoopControllerRealLoop::QuitLoopClosure() {
  return [this] { loop_.Quit(); };
}

bool LoopControllerRealLoop::RunLoopUntil(fit::function<bool()> condition) {
  while (true) {
    if (condition()) {
      return true;
    }
    RunGivenLoopWithTimeout(&loop_, zx::msec(10));
  }
}

bool LoopControllerRealLoop::RunLoopFor(zx::duration duration) {
  return RunGivenLoopWithTimeout(&loop_, duration);
}

}  // namespace ledger
