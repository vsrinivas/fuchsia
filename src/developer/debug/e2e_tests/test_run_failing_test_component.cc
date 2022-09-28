// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "src/developer/debug/e2e_tests/e2e_test.h"
#include "src/developer/debug/zxdb/client/process.h"

namespace zxdb {

namespace {
constexpr std::string_view kRunTestCommand =
    "run-test fuchsia-pkg://fuchsia.com/crasher_test#meta/cpp_crasher_test.cm "
    "CrasherTest.ShouldFail";
constexpr std::string_view kCrasherTestSymbolPathSuffix = "exe.unstripped/crasher_test";

// Test component testing functionality. Launch a component test package that will always fail and
// make sure we make and catch an exception.
class RunFailingTestComponent : public E2eTest {
 public:
  RunFailingTestComponent() {
    // Add symbols for crasher_test.
    ConfigureSymbolsWithFile(kCrasherTestSymbolPathSuffix);
  }

  void Run() {
    // Actually kick off inferior program.
    console().ProcessInputLine(kRunTestCommand.data(), [this](const Err& e) {
      auto target = console().context().GetActiveTarget();
      ASSERT_NE(target, nullptr);
    });

    loop().Run();
  }

  // ProcessObserver implementation.
  void DidCreateProcess(Process* process, uint64_t timestamp) override {
    // The process has been created, let's make sure the console is attached.
    auto active_process = console().context().GetActiveTarget()->GetProcess();
    EXPECT_EQ(active_process->GetKoid(), process->GetKoid());
  }

  void WillDestroyProcess(Process* process, DestroyReason reason, int exit_code,
                          uint64_t timestamp) override {
    EXPECT_EQ(reason, ProcessObserver::DestroyReason::kKill);

    // TODO(fxbug.dev/110651): Remove this once "OnTestComponentExited" notification is implemented.
    // After killing the test component, debug_agent needs to stay alive long enough for test_runner
    // to gracefully shutdown. If debug_agent doesn't wait long enough, test_runner will spam the
    // logs with lots of warnings since the debug_agent handlers didn't consume all of the events
    // because they had already gone out scope and been deleted.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Quit the MessageLoop to end the test.
    debug::MessageLoop::Current()->QuitNow();
  }

  // ThreadObserver implementation.
  void OnThreadStopped(Thread* thread, const StopInfo& info) override {
    // The crasher unit test case fails by default. Make sure we caught the exception, then kill it.
    EXPECT_EQ(info.exception_type, debug_ipc::ExceptionType::kSoftwareBreakpoint);

    console().ProcessInputLine("kill", [](const Err& e) { EXPECT_TRUE(e.ok()) << e.msg(); });
  }
};
}  // namespace

TEST_F(RunFailingTestComponent, RunFailingTestComponent) { Run(); }

}  // namespace zxdb
