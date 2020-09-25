// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/service_directory.h>

#include <gtest/gtest.h>

#include "launcher.h"

namespace {

void TestSingleComponent(std::string url) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto svc = sys::ServiceDirectory::CreateFromNamespace();

  fuchsia::sys::LauncherPtr launcher;
  zx_status_t status = svc->Connect(launcher.NewRequest());
  ASSERT_EQ(ZX_OK, status);

  fuchsia::sys::LaunchInfo info{
      .url = std::move(url),
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

TEST(IntegrationTest, HlcppSync) {
  ASSERT_EQ(
      LaunchComponents("fuchsia-pkg://fuchsia.com/echo-hlcpp-client-sync#meta/echo-client.cmx",
                       "fuchsia-pkg://fuchsia.com/echo-hlcpp-server#meta/echo-server.cmx",
                       {"fuchsia.examples.Echo"}),
      0);
}

TEST(IntegrationTest, HlcppAsync) {
  ASSERT_EQ(LaunchComponents("fuchsia-pkg://fuchsia.com/echo-hlcpp-client#meta/echo-client.cmx",
                             "fuchsia-pkg://fuchsia.com/echo-hlcpp-server#meta/echo-server.cmx",
                             {"fuchsia.examples.Echo"}),
            0);
}

TEST(IntegrationTest, HlcppService) {
  TestSingleComponent("fuchsia-pkg://fuchsia.com/echo-hlcpp-service-client#meta/echo-client.cmx");
}

TEST(IntegrationTest, HlcppMultipleClients) {
  ASSERT_EQ(
      LaunchComponents("fuchsia-pkg://fuchsia.com/echo-hlcpp-multi-client#meta/echo-client.cmx",
                       "fuchsia-pkg://fuchsia.com/echo-hlcpp-multi-server#meta/echo-server.cmx",
                       {"fuchsia.examples.Echo"}),
      0);
}

TEST(IntegrationTest, HlcppPipelining) {
  ASSERT_EQ(
      LaunchComponents("fuchsia-pkg://fuchsia.com/echo-launcher-hlcpp-client#meta/echo-client.cmx",
                       "fuchsia-pkg://fuchsia.com/echo-launcher-hlcpp-server#meta/echo-server.cmx",
                       {"fuchsia.examples.EchoLauncher"}),
      0);
}

TEST(IntegrationTest, LlcppAsync) {
  ASSERT_EQ(LaunchComponents("fuchsia-pkg://fuchsia.com/echo-llcpp-client#meta/echo-client.cmx",
                             "fuchsia-pkg://fuchsia.com/echo-llcpp-server#meta/echo-server.cmx",
                             {"fuchsia.examples.Echo"}),
            0);
}

TEST(IntegrationTest, LlcppSync) {
  ASSERT_EQ(
      LaunchComponents("fuchsia-pkg://fuchsia.com/echo-llcpp-client-sync#meta/echo-client.cmx",
                       "fuchsia-pkg://fuchsia.com/echo-llcpp-server#meta/echo-server.cmx",
                       {"fuchsia.examples.Echo"}),
      0);
}

TEST(IntegrationTest, LlcppService) {
  TestSingleComponent("fuchsia-pkg://fuchsia.com/echo-llcpp-service-client#meta/echo-client.cmx");
}

TEST(IntegrationTest, LlcppPipelining) {
  ASSERT_EQ(
      LaunchComponents("fuchsia-pkg://fuchsia.com/echo-launcher-llcpp-client#meta/echo-client.cmx",
                       "fuchsia-pkg://fuchsia.com/echo-launcher-llcpp-server#meta/echo-server.cmx",
                       {"fuchsia.examples.EchoLauncher"}),
      0);
}

TEST(IntegrationTest, LlcppAsyncCompleter) {
  ASSERT_EQ(
      LaunchComponents("fuchsia-pkg://fuchsia.com/echo-llcpp-client-async#meta/echo-client.cmx",
                       "fuchsia-pkg://fuchsia.com/echo-llcpp-server-async#meta/echo-server.cmx",
                       {"fuchsia.examples.Echo"}),
      0);
}

TEST(IntegrationTest, RustAsync) {
  ASSERT_EQ(LaunchComponents("fuchsia-pkg://fuchsia.com/echo-rust-client#meta/echo-client.cmx",
                             "fuchsia-pkg://fuchsia.com/echo-rust-server#meta/echo-server.cmx",
                             {"fuchsia.examples.Echo"}),
            0);
}

TEST(IntegrationTest, RustSync) {
  ASSERT_EQ(LaunchComponents("fuchsia-pkg://fuchsia.com/echo-rust-client-sync#meta/echo-client.cmx",
                             "fuchsia-pkg://fuchsia.com/echo-rust-server#meta/echo-server.cmx",
                             {"fuchsia.examples.Echo"}),
            0);
}

TEST(IntegrationTest, RustService) {
  TestSingleComponent("fuchsia-pkg://fuchsia.com/echo-rust-service-client#meta/echo-client.cmx");
}

TEST(IntegrationTest, RustPipelining) {
  ASSERT_EQ(
      LaunchComponents("fuchsia-pkg://fuchsia.com/echo-launcher-rust-client#meta/echo-client.cmx",
                       "fuchsia-pkg://fuchsia.com/echo-launcher-rust-server#meta/echo-server.cmx",
                       {"fuchsia.examples.EchoLauncher"}),
      0);
}

TEST(IntegrationTest, GoSync) {
  ASSERT_EQ(LaunchComponents("fuchsia-pkg://fuchsia.com/echo-go-client#meta/echo-client.cmx",
                             "fuchsia-pkg://fuchsia.com/echo-go-server#meta/echo-server.cmx",
                             {"fuchsia.examples.Echo"}),
            0);
}

TEST(IntegrationTest, GoPipelining) {
  ASSERT_EQ(
      LaunchComponents("fuchsia-pkg://fuchsia.com/echo-launcher-go-client#meta/echo-client.cmx",
                       "fuchsia-pkg://fuchsia.com/echo-launcher-go-server#meta/echo-server.cmx",
                       {"fuchsia.examples.EchoLauncher"}),
      0);
}
