// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/public/lib/modular_test_harness/cpp/test_harness_launcher.h"

namespace modular {
namespace testing {
namespace {
constexpr char kTestHarnessUrl[] =
    "fuchsia-pkg://fuchsia.com/modular_test_harness#meta/"
    "modular_test_harness.cmx";
};

TestHarnessLauncher::TestHarnessLauncher()
    : harness_launcher_thread_(
          [this](fidl::InterfaceRequest<fuchsia::modular::testing::TestHarness>
                     test_harness_req) {
            LaunchTestHarness(std::move(test_harness_req));
          },
          test_harness_.NewRequest()) {}

TestHarnessLauncher::~TestHarnessLauncher() {
  // Unbinding TestHarness should cause modular_test_harness.cmx to exit, and
  // consequently exit |harness_launcher_thread_|.
  test_harness_.Unbind();

  harness_launcher_thread_.join();
}

void TestHarnessLauncher::LaunchTestHarness(
    fidl::InterfaceRequest<fuchsia::modular::testing::TestHarness>
        test_harness_req) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = kTestHarnessUrl;
  test_harness_svc_ =
      sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);

  auto svc = sys::ServiceDirectory::CreateFromNamespace();
  svc->Connect<fuchsia::sys::Launcher>()->CreateComponent(
      std::move(launch_info), test_harness_ctrl_.NewRequest());

  test_harness_svc_->Connect(std::move(test_harness_req));

  test_harness_ctrl_.events().OnTerminated =
      [&](int64_t return_code, fuchsia::sys::TerminationReason reason) {
        loop.Quit();
      };
  test_harness_ctrl_.set_error_handler([&](zx_status_t) { loop.Quit(); });

  loop.Run();
}

}  // namespace testing
}  // namespace modular
