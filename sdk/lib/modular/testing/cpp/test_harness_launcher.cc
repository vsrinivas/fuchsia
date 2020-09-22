// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>
#include <lib/modular/testing/cpp/test_harness_launcher.h>

namespace modular_testing {

namespace {

constexpr char kTestHarnessUrl[] =
    "fuchsia-pkg://fuchsia.com/modular_test_harness#meta/"
    "modular_test_harness.cmx";

constexpr zx::duration kTerminateTimeout = zx::sec(10);

}  // namespace

TestHarnessLauncher::TestHarnessLauncher(fuchsia::sys::LauncherPtr launcher)
    : test_harness_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
  test_harness_loop_.StartThread();

  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kTestHarnessUrl;
  test_harness_svc_ = sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);

  // Bind |test_harness_ctrl_| to |test_harness_loop_|, so that its error handler is
  // dispatched even while this thread is blocked in the destructor.
  launcher->CreateComponent(std::move(launch_info),
                            test_harness_ctrl_.NewRequest(test_harness_loop_.dispatcher()));

  test_harness_svc_->Connect(test_harness_.NewRequest());
  test_harness_svc_->Connect(lifecycle_.NewRequest());

  test_harness_ctrl_.set_error_handler(
      [this](zx_status_t /*unused*/) { test_harness_loop_.Quit(); });
}

void TestHarnessLauncher::StopTestHarness() {
  if (!is_test_harness_running()) {
    return;
  }

  if (lifecycle_) {
    lifecycle_->Terminate();
    // Upon Lifecycle/Terminate(), the modular test harness will ask basemgr to terminate, and
    // force-kill it if it doesn't terminate after some time.

    // In case |lifecycle_| is closed by the test harness component but the error handler hasn't
    // been dispatched yet, we kick off a timer to kill the test harness component.
    async::PostDelayedTask(
        test_harness_loop_.dispatcher(), [this] { test_harness_ctrl_->Kill(); }, kTerminateTimeout);
  } else {
    async::PostTask(test_harness_loop_.dispatcher(), [this] { test_harness_ctrl_->Kill(); });
  }
}

TestHarnessLauncher::~TestHarnessLauncher() {
  StopTestHarness();

  test_harness_loop_.JoinThreads();
}

}  // namespace modular_testing
