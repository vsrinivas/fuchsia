// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atomic>

#include <gtest/gtest.h>

#include "src/developer/debug/e2e_tests/e2e_test.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/client/breakpoint.h"
#include "src/developer/debug/zxdb/client/filter.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/client/thread.h"

namespace zxdb {

namespace {

// This test steps over "step_plt.cc" from line 12 to line 14. See fxbug.dev/112187.

class StepPLT : public E2eTest {
 public:
  StepPLT() { ConfigureSymbolsWithFile("exe.unstripped/step_plt"); }

  void Run() {
    expected_stopped_line_ = 12;
    console().ProcessInputLine("break step_plt.cc:12");
    console().ProcessInputLine(
        "run-component fuchsia-pkg://fuchsia.com/zxdb_e2e_inferiors#meta/step_plt.cm");

    loop().Run();
  }

  void OnThreadStopped(Thread* thread, const StopInfo& info) override {
    FX_LOGS(INFO) << "OnThreadStopped  expected_stopped_line_=" << expected_stopped_line_;

    const Stack& stack = thread->GetStack();
    ASSERT_FALSE(stack.empty());
    const Location& location = stack[0]->GetLocation();
    EXPECT_EQ(location.file_line().line(), expected_stopped_line_);
    if (expected_stopped_line_ < 14) {
      expected_stopped_line_++;
      console().ProcessInputLine("next");
    } else {
      console().ProcessInputLine("continue");
    }
  }

  void WillDestroyProcess(Process* process, DestroyReason reason, int exit_code,
                          uint64_t timestamp) override {
    EXPECT_EQ(expected_stopped_line_, 14);

    loop().QuitNow();
  }

 private:
  int expected_stopped_line_;
};

}  // namespace

TEST_F(StepPLT, StepPLT) { Run(); }

}  // namespace zxdb
