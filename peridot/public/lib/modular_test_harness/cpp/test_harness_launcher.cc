// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/public/lib/modular_test_harness/cpp/test_harness_launcher.h"

#include <src/lib/fxl/logging.h>

#include "lib/async/cpp/task.h"

namespace modular {
namespace testing {
namespace {
constexpr char kTestHarnessUrl[] =
    "fuchsia-pkg://fuchsia.com/modular_test_harness#meta/"
    "modular_test_harness.cmx";
};

TestHarnessLauncher::TestHarnessLauncher(fuchsia::sys::LauncherPtr launcher) {
  harness_launcher_thread_.reset(new std::thread(
      [this](fuchsia::sys::LauncherPtr launcher,
             fidl::InterfaceRequest<fuchsia::modular::testing::TestHarness>
                 test_harness_req) {
        LaunchTestHarness(std::move(launcher), std::move(test_harness_req));
      },
      std::move(launcher), test_harness_.NewRequest()));
}

TestHarnessLauncher::~TestHarnessLauncher() {
  // Wait until the test harness thread's async::Loop is ready
  {
    std::unique_lock<std::mutex> lock(test_harness_loop_mutex_);
    test_harness_loop_cv_.wait(
        lock, [this] { return test_harness_loop_ != nullptr; });
  }

  // Tell the thread to kill the modular test harness -- the thread should exit
  // once it gets an event that the modular test harness terminated.
  async::PostTask(test_harness_loop_->dispatcher(),
                  [this] { test_harness_ctrl_->Kill(); });

  // Wait for the thread to exit.
  harness_launcher_thread_->join();
}

void TestHarnessLauncher::LaunchTestHarness(
    fuchsia::sys::LauncherPtr launcher,
    fidl::InterfaceRequest<fuchsia::modular::testing::TestHarness>
        test_harness_req) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  {
    std::unique_lock<std::mutex> lock(test_harness_loop_mutex_);
    test_harness_loop_ = &loop;
    test_harness_loop_cv_.notify_one();
  }

  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kTestHarnessUrl;
  test_harness_svc_ =
      sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);

  launcher->CreateComponent(std::move(launch_info),
                            test_harness_ctrl_.NewRequest());
  test_harness_svc_->Connect(std::move(test_harness_req));

  // Exit the loop (and therefore the thread), if the modular test harness
  // component exits.
  test_harness_ctrl_.set_error_handler([&](zx_status_t) { loop.Quit(); });

  loop.Run();
}

}  // namespace testing
}  // namespace modular
