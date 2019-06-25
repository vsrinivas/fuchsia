// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>
#include <lib/modular/testing/cpp/test_harness_launcher.h>

namespace modular {
namespace testing {
namespace {
constexpr char kTestHarnessUrl[] =
    "fuchsia-pkg://fuchsia.com/modular_test_harness#meta/"
    "modular_test_harness.cmx";
};

TestHarnessLauncher::TestHarnessLauncher(fuchsia::sys::LauncherPtr launcher)
    : test_harness_loop_(&kAsyncLoopConfigNoAttachToThread) {
  test_harness_loop_.StartThread();

  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kTestHarnessUrl;
  test_harness_svc_ =
      sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);

  launcher->CreateComponent(
      std::move(launch_info),
      test_harness_ctrl_.NewRequest(test_harness_loop_.dispatcher()));

  test_harness_svc_->Connect(test_harness_.NewRequest());

  test_harness_ctrl_.set_error_handler(
      [this](zx_status_t) { test_harness_loop_.Quit(); });
}

TestHarnessLauncher::~TestHarnessLauncher() {
  test_harness_ctrl_->Kill();
  test_harness_loop_.JoinThreads();
}

}  // namespace testing
}  // namespace modular
