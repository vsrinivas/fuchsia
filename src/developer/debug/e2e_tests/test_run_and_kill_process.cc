// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <atomic>

#include <gtest/gtest.h>

#include "src/developer/debug/e2e_tests/e2e_test.h"
#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/client/breakpoint.h"
#include "src/developer/debug/zxdb/client/filter.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/console/command_sequence.h"
#include "src/developer/debug/zxdb/console/mock_console.h"

namespace zxdb {

namespace {
constexpr std::string_view kCrasherSymbolPathSuffix = "exe.unstripped/crasher";

// This is a very simple test to ensure basic functionality of commonly used commands. This
// simulates a user passing a filter to "attach", setting a breakpoint at a well-known symbol (a
// function name in this case), and then running the program with the "run" command. The test
// ensures that the breakpoint is eventually matched when symbols are loaded, and then that the
// breakpoint was hit before killing the program.
class RunAndKillProcess : public E2eTest {
 public:
  RunAndKillProcess() {
    // Add symbols for crasher.
    ConfigureSymbolsWithFile(kCrasherSymbolPathSuffix);
  }

  void Run() {
    auto command_sequence_ctx =
        fxl::MakeRefCounted<ConsoleCommandContext>(&console(), [this](const Err& e) {
          // Make sure the filter has been set up.
          EXPECT_TRUE(e.ok()) << e.msg();
          auto filters = this->session().system().GetFilters();
          EXPECT_EQ(filters.size(), 1u);
          EXPECT_STREQ(filters[0]->pattern().c_str(), "crasher");

          // Make sure the breakpoint was added, but hasn't been resolved to a location yet.
          EXPECT_TRUE(e.ok()) << e.msg();
          auto bps = this->session().system().GetBreakpoints();
          ASSERT_EQ(bps.size(), 1u);
          ASSERT_NE(bps[0], nullptr);
          EXPECT_EQ(this->console().context().GetActiveBreakpoint(), bps[0]);
          EXPECT_EQ(bps[0]->GetSettings().scope.type(), ExecutionScope::kSystem);
          EXPECT_EQ(bps[0]->GetSettings().stop_mode, BreakpointSettings::StopMode::kAll);

          // Shouldn't have any resolved locations yet, because symbols haven't been loaded yet.
          EXPECT_EQ(bps[0]->GetLocations().size(), 0u);
        });

    RunCommandSequence(&console(), {"attach crasher", "break blind_write"},
                       std::move(command_sequence_ctx));

    auto run_ctx = fxl::MakeRefCounted<ConsoleCommandContext>(&console(), [this](const Err& e) {
      auto target = this->console().context().GetActiveTarget();
      ASSERT_NE(target, nullptr);
      EXPECT_STREQ(target->GetProcess()->GetName().c_str(), "/boot/bin/crasher");
    });
    console().ProcessInputLine("run /boot/bin/crasher", std::move(run_ctx));

    // Kick off the MessageLoop, we should catch the process starting in the above observer
    // implementations.
    loop().Run();
  }

  // BreakpointObserver implementation. This observer method should be called first.
  void OnBreakpointMatched(Breakpoint* breakpoint, bool user_requested) override {
    ASSERT_NE(breakpoint, nullptr);

    auto threads = console().context().GetActiveTarget()->GetProcess()->GetThreads();
    ASSERT_GT(threads.size(), 0u);

    // All threads should be blocked when we hit the breakpoint.
    for (auto& thread : threads) {
      EXPECT_EQ(thread->GetState(), debug_ipc::ThreadRecord::State::kBlocked);
    }

    auto current_bp = console().context().GetActiveBreakpoint();

    // Should be the same breakpoint that we just installed.
    EXPECT_EQ(current_bp->GetStats().id, breakpoint->GetStats().id);

    // This breakpoint should match something (could be more than 1) now.
    EXPECT_GE(breakpoint->GetLocations().size(), 1u);

    // Because the breakpoint wasn't matched at the time the user issued the "break" command, this
    // is not considered user requested.
    EXPECT_FALSE(user_requested);

    // We shouldn't have hit the breakpoint yet since this notification is dispatched when the
    // breakpoint actually matches a symbol.
    EXPECT_EQ(breakpoint->GetStats().hit_count, 0u);
  }

  // ThreadObserver implementation.
  void OnThreadStopped(Thread* thread, const StopInfo& info) override {
    ASSERT_NE(thread, nullptr);

    // We should have hit our breakpoint.
    EXPECT_EQ(info.exception_type, debug_ipc::ExceptionType::kSoftwareBreakpoint);

    auto target = console().context().GetActiveTarget();
    auto active_thread = console().context().GetActiveThreadForTarget(target);

    EXPECT_EQ(active_thread->GetKoid(), thread->GetKoid());
    EXPECT_TRUE(thread->IsBlockedOnException());
    EXPECT_EQ(thread->GetBlockedReason(), debug_ipc::ThreadRecord::BlockedReason::kException);

    console().ProcessInputLine("frame", nullptr);

    console().ProcessInputLine("kill", nullptr);
  }

  // ProcessObserver implementation.
  void WillDestroyProcess(Process* process, DestroyReason reason, int exit_code,
                          uint64_t timestamp) override {
    ASSERT_NE(process, nullptr);
    EXPECT_EQ(process, console().context().GetActiveTarget()->GetProcess());
    EXPECT_EQ(reason, ProcessObserver::DestroyReason::kKill);

    // Quit the MessageLoop to end the test.
    debug::MessageLoop::Current()->QuitNow();
  }
};

}  // namespace

TEST_F(RunAndKillProcess, RunAndKillProcess) { Run(); }

}  // namespace zxdb
