// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "src/developer/debug/e2e_tests/e2e_test.h"
#include "src/developer/debug/zxdb/client/filter.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/thread.h"

namespace zxdb {

namespace {
constexpr std::string_view kCrasherTestComponentUrl =
    "fuchsia-pkg://fuchsia.com/crasher_test#meta/cpp_crasher_test.cm";
constexpr std::string_view kRunTestCommand =
    "run-test fuchsia-pkg://fuchsia.com/crasher_test#meta/cpp_crasher_test.cm "
    "CrasherTest.ShouldFail";

// Test component testing functionality. Launch a component test package that will always fail and
// make sure we make and catch an exception.
class RunFailingTestComponent : public E2eTest {
 public:
  RunFailingTestComponent() {
    // Add symbols for crasher_test.
    ConfigureSymbolsWithFile("exe.unstripped/crasher_test");
  }

  void Run() {
    // Actually kick off inferior program.
    console().ProcessInputLine(kRunTestCommand.data());

    loop().Run();
  }

  // ProcessObserver implementation.
  void DidCreateProcess(Process* process, uint64_t timestamp) override {
    ASSERT_TRUE(process->GetComponentInfo());
    EXPECT_STREQ(process->GetComponentInfo()->url.c_str(), kCrasherTestComponentUrl.data());
    // The process has been created, let's make sure the console is attached.
    auto active_process = console().context().GetActiveTarget()->GetProcess();
    EXPECT_EQ(active_process->GetKoid(), process->GetKoid());
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
    console().ProcessInputLine("kill");
  }

  void WillDestroyProcess(Process* process, DestroyReason reason, int exit_code,
                          uint64_t timestamp) override {
    EXPECT_EQ(reason, ProcessObserver::DestroyReason::kKill);
  }

  // ComponentObserver implementation. Note: we must have a filter for the component we care about.
  void OnTestExited(const std::string& url) override {
    EXPECT_STREQ(url.c_str(), kCrasherTestComponentUrl.data());

    // Quit the MessageLoop to end the test. When we get this notification, the test_runner should
    // be fully shutdown and there should be no more pending messages from it. We can now teardown
    // debug_agent gracefully.
    debug::MessageLoop::Current()->QuitNow();
  }
};
}  // namespace

TEST_F(RunFailingTestComponent, RunFailingTestComponent) { Run(); }

}  // namespace zxdb
