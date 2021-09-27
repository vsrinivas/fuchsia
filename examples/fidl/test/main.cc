// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/service_directory.h>

#include <gtest/gtest.h>

#include "launcher.h"

TEST(IntegrationTest, DartAsync) {
  ASSERT_EQ(LaunchComponents("fuchsia-pkg://fuchsia.com/echo-dart-client#meta/echo-dart-client.cmx",
                             "fuchsia-pkg://fuchsia.com/echo-dart-server#meta/echo-dart-server.cmx",
                             {"fuchsia.examples.Echo"}),
            0);
}

TEST(IntegrationTest, DartPipelining) {
  ASSERT_EQ(
      LaunchComponents(
          "fuchsia-pkg://fuchsia.com/echo-launcher-dart-client#meta/echo-launcher-dart-client.cmx",
          "fuchsia-pkg://fuchsia.com/echo-launcher-dart-server#meta/echo-launcher-dart-server.cmx",
          {"fuchsia.examples.EchoLauncher"}),
      0);
}
