// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "src/developer/debug/e2e_tests/e2e_test.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/thread.h"

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
    console().ProcessInputLine(kRunTestCommand.data(), nullptr);

    loop().Run();
  }

  // ProcessObserver implementation.
  void DidCreateProcess(Process* process, uint64_t timestamp) override {
    // The process has been created, let's make sure the console is attached.
    auto active_process = console().context().GetActiveTarget()->GetProcess();
    EXPECT_EQ(active_process->GetKoid(), process->GetKoid());
  }

  void OnBreakpointMatched(Breakpoint* bp, bool user_requested) override {}

  void WillDestroyProcess(Process* process, DestroyReason reason, int exit_code,
                          uint64_t timestamp) override {
    EXPECT_EQ(reason, ProcessObserver::DestroyReason::kKill);

    // TODO(fxbug.dev/110651): Remove this once "OnTestComponentExited" notification is implemented.
    // After killing the test component, debug_agent needs to stay alive long enough for test_runner
    // to gracefully shutdown. If debug_agent doesn't wait long enough, test_runner will spam the
    // logs with lots of warnings since the debug_agent handlers didn't consume all of the events
    // because they had already gone out scope.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Quit the MessageLoop to end the test.
    debug::MessageLoop::Current()->QuitNow();
  }

  // ThreadObserver implementation.
  void OnThreadStopped(Thread* thread, const StopInfo& info) override {
    // TODO(fxbug.dev/111788): Remove the check for PageFault.
    // There could be three stoppages:
    //  1. A software breakpoint for the test failure.
    //  2. A PageFault exception for dereferencing a nullptr.
    //  3. Another PageFault exception for second chance exception handling.
    EXPECT_TRUE((info.exception_type == debug_ipc::ExceptionType::kSoftwareBreakpoint) ||
                (info.exception_type == debug_ipc::ExceptionType::kPageFault));
    console().ProcessInputLine("kill", nullptr);
  }
};
}  // namespace

TEST_F(RunFailingTestComponent, RunFailingTestComponent) { Run(); }

}  // namespace zxdb
