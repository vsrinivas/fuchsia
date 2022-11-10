// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/e2e_tests/e2e_test.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/thread.h"

namespace zxdb {

namespace {

// This test set a breakpoint on _dl_start to verify that we can debug early-stage libc code.

class BreakDlStart : public E2eTest {
 public:
  void Run() {
    console().ProcessInputLine("break _dl_start");
    console().ProcessInputLine(
        "run-component fuchsia-pkg://fuchsia.com/zxdb_e2e_inferiors#meta/step_plt.cm");

    loop().Run();
  }

  void OnThreadStopped(Thread* thread, const StopInfo& info) override {
    FX_LOGS(INFO) << "OnThreadStopped";
    const Stack& stack = thread->GetStack();
    ASSERT_FALSE(stack.empty());
    const Location& location = stack[0]->GetLocation();
    EXPECT_TRUE(location.symbol().is_valid());
    EXPECT_EQ("_dl_start", location.symbol().Get()->GetFullName());
    console().ProcessInputLine("kill");
  }

  void WillDestroyProcess(Process* process, DestroyReason reason, int exit_code,
                          uint64_t timestamp) override {
    EXPECT_EQ(reason, DestroyReason::kKill)
        << "The process is not killed. Maybe breakpoint doesn't work?";

    loop().QuitNow();
  }
};

}  // namespace

TEST_F(BreakDlStart, BreakDlStart) { Run(); }

}  // namespace zxdb
