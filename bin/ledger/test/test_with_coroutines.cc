// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/test/test_with_coroutines.h"

#include "lib/fsl/tasks/message_loop.h"

namespace test {

namespace {

// Wrapper around a real CoroutineHandler for test.
//
// The wrapper allows to delay re-entering the coroutine body when the run loop
// is running. When |Continue| is called, it quits the loop, and the main method
// calls |ContinueIfNeeded| when the loop exits.
class TestCoroutineHandler : public coroutine::CoroutineHandler {
 public:
  TestCoroutineHandler(coroutine::CoroutineHandler* delegate)
      : delegate_(delegate) {}

  bool Yield() override { return delegate_->Yield(); }

  void Continue(bool interrupt) override {
    // If interrupting, no need to delay the call as the test will not run the
    // loop itself.
    if (interrupt) {
      delegate_->Continue(interrupt);
      return;
    }
    fsl::MessageLoop::GetCurrent()->QuitNow();
    need_to_continue_ = true;
  }

  // Re-enters the coroutine body if the handler delayed the call.
  void ContinueIfNeeded() {
    if (need_to_continue_) {
      need_to_continue_ = false;
      delegate_->Continue(false);
    }
  }

 private:
  coroutine::CoroutineHandler* delegate_;
  bool need_to_continue_ = false;
};

}  // namespace

TestWithCoroutines::TestWithCoroutines() {}

bool TestWithCoroutines::RunInCoroutine(
    std::function<void(coroutine::CoroutineHandler*)> run_test) {
  std::unique_ptr<TestCoroutineHandler> test_handler;
  volatile bool ended = false;
  coroutine_service_.StartCoroutine([&](coroutine::CoroutineHandler* handler) {
    test_handler = std::make_unique<TestCoroutineHandler>(handler);
    run_test(test_handler.get());
    ended = true;
  });
  return RunLoopUntil([&] {
    if (!ended) {
      test_handler->ContinueIfNeeded();
    }
    return ended;
  });
}

}  // namespace test
