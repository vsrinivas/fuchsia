// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/testing/loop_controller_real_loop.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/zx/time.h>

#include "peridot/bin/ledger/testing/blocking_callback_waiter.h"

namespace ledger {

namespace {

// Returns false if the loop returned early.
bool RunGivenLoopUntil(async::Loop* loop, zx::time time) {
  bool timed_out = false;
  async::TaskClosure task([loop, &timed_out] {
    timed_out = true;
    loop->Quit();
  });
  task.PostForTime(loop->dispatcher(), time);
  loop->Run();
  loop->ResetQuit();
  // Another task can call Quit() on the message loop, which exits the
  // message loop before the delayed task executes, in which case |timed_out| is
  // still false here because the delayed task hasn't run yet. Returning from
  // this function will delete |task| which will unregister it from the loop.
  return timed_out;
}

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

}  // namespace

LoopControllerRealLoop::LoopControllerRealLoop()
    : loop_(&kAsyncLoopConfigAttachToThread) {}

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
    RunGivenLoopUntil(&loop_, zx::clock::get_monotonic() + zx::msec(10));
  }
}

void LoopControllerRealLoop::RunLoopFor(zx::duration duration) {
  zx::time deadline = zx::clock::get_monotonic() + duration;
  while (!RunGivenLoopUntil(&loop_, deadline)) {
    // Do nothing
  }
}

}  // namespace ledger
