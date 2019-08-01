// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/cpp/service_directory.h>

#include <gtest/gtest.h>

namespace fidl {
namespace {

TEST(IntegrationTest, Echo) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto svc = sys::ServiceDirectory::CreateFromNamespace();

  fuchsia::sys::LauncherPtr launcher;
  zx_status_t status = svc->Connect(launcher.NewRequest());
  ASSERT_EQ(ZX_OK, status);

  fuchsia::sys::LaunchInfo info{
      .url = "fuchsia-pkg://fuchsia.com/echo_client#meta/echo_client.cmx",
  };
  fuchsia::sys::ComponentControllerPtr controller;
  launcher->CreateComponent(std::move(info), controller.NewRequest());

  controller.events().OnTerminated = [&loop](int64_t code, fuchsia::sys::TerminationReason reason) {
    loop.Quit();
    ASSERT_EQ(0, code);
    ASSERT_EQ(fuchsia::sys::TerminationReason::EXITED, reason);
  };
  loop.Run();
}

}  // namespace
}  // namespace fidl
