// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/service_directory.h>
#include <stdio.h>

#include <fidl/examples/routing/echo/cpp/fidl.h>
#include <gtest/gtest.h>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/binding.h"

void test_helper(int count) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto service = sys::ServiceDirectory::CreateFromNamespace();
  fidl::examples::routing::echo::EchoSyncPtr echo;
  service->Connect(echo.NewRequest());

  fidl::StringPtr response = "";
  std::string msg = "test_str" + std::to_string(count);
  ASSERT_EQ(ZX_OK, echo->EchoString(msg, &response));
  EXPECT_EQ(response, msg.c_str());
}

TEST(EchoTest, TestEcho1) { test_helper(1); }

TEST(EchoTest, TestEcho2) { test_helper(2); }

TEST(EchoTest, TestEcho3) { test_helper(3); }

TEST(EchoTest, TestEcho4) { test_helper(4); }

TEST(EchoTest, TestEcho5) { test_helper(5); }
