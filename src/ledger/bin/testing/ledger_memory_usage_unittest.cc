// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/testing/ledger_memory_usage.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <stdlib.h>

#include "gtest/gtest.h"
#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/testing/get_ledger.h"

namespace ledger {
namespace {

int64_t LaunchTestBenchmark(async::Loop* loop) {
  auto component_context = sys::ComponentContext::Create();
  fuchsia::sys::ComponentControllerPtr component_controller;
  scoped_tmpfs::ScopedTmpFS tmp_dir;

  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = "fuchsia-pkg://fuchsia.com/trace#meta/trace.cmx";
  launch_info.arguments.push_back("record");
  launch_info.arguments.push_back(
      "--spec-file="
      "/pkgfs/packages/ledger_tests/0/data/memory_usage_test_benchmark.tspec");

  fuchsia::sys::LauncherPtr launcher;
  component_context->svc()->Connect(launcher.NewRequest());
  launcher->CreateComponent(std::move(launch_info),
                            component_controller.NewRequest());

  int64_t return_code = INT64_MIN;
  component_controller.events().OnTerminated =
      [loop, &return_code](int64_t rc,
                           fuchsia::sys::TerminationReason termination_reason) {
        switch (termination_reason) {
          case fuchsia::sys::TerminationReason::UNKNOWN:
            return_code = EXIT_FAILURE;
            break;
          case fuchsia::sys::TerminationReason::EXITED:
            return_code = rc;
            break;
          default:
            return_code = static_cast<int64_t>(termination_reason);
            break;
        }
        loop->Quit();
      };
  loop->Run();
  return return_code;
}

TEST(LedgerMemoryUsage, Simple) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  EXPECT_EQ(EXIT_SUCCESS, LaunchTestBenchmark(&loop));
}

TEST(LedgerMemoryUsage, LaunchTwoLedgers) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto component_context = sys::ComponentContext::Create();
  fuchsia::sys::ComponentControllerPtr component_controller;
  scoped_tmpfs::ScopedTmpFS tmp_dir;

  // Start a first Ledger instance.
  LedgerPtr top_level_ledger;
  fit::function<void()> error_handler = [] { ADD_FAILURE(); };
  Status status = GetLedger(component_context.get(),
                            component_controller.NewRequest(), nullptr, "",
                            "top_level_ledger", DetachedPath(tmp_dir.root_fd()),
                            std::move(error_handler), &top_level_ledger);
  ASSERT_EQ(Status::OK, status);

  // The test benchmark will start another Ledger instance and try to get the
  // memory usage from that one. Ensure this operation succeeds.
  EXPECT_EQ(EXIT_SUCCESS, LaunchTestBenchmark(&loop));
}

}  // namespace
}  // namespace ledger
