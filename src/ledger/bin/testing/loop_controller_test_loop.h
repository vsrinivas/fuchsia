// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_TESTING_LOOP_CONTROLLER_TEST_LOOP_H_
#define SRC_LEDGER_BIN_TESTING_LOOP_CONTROLLER_TEST_LOOP_H_

#include <lib/async-testutils/test_loop.h>

#include <memory>

#include "src/ledger/bin/testing/loop_controller.h"

namespace ledger {

// Implementation of a LoopController that uses a test loop. The test loop
// simulates the time in a deterministic way and does not rely on the real
// (physical) clock.
class LoopControllerTestLoop : public LoopController {
 public:
  LoopControllerTestLoop(async::TestLoop* loop);
  ~LoopControllerTestLoop() override;

  void RunLoop() override;

  void StopLoop() override;

  std::unique_ptr<SubLoop> StartNewLoop() override;

  std::unique_ptr<CallbackWaiter> NewWaiter() override;

  async_dispatcher_t* dispatcher() override;

  bool RunLoopUntil(fit::function<bool()> condition) override;

  void RunLoopFor(zx::duration duration) override;

  async::TestLoop& test_loop() { return *loop_; }

 private:
  async::TestLoop* const loop_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_TESTING_LOOP_CONTROLLER_TEST_LOOP_H_
