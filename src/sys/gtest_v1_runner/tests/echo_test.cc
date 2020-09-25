// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/service_directory.h>

#include <fidl/examples/echo/cpp/fidl.h>
#include <gtest/gtest.h>

#include "lib/fidl/cpp/binding.h"

TEST(EchoTest, TestEcho) {
  printf("hello echo\n");
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto service = sys::ServiceDirectory::CreateFromNamespace();
  fidl::examples::echo::EchoSyncPtr echo;
  service->Connect(echo.NewRequest());

  fidl::StringPtr response = "";
  ASSERT_EQ(ZX_OK, echo->EchoString("test string", &response));
  EXPECT_EQ(response, "test string");
}

TEST(EchoTest, TestEcho2) {}

TEST(EchoTest, DISABLED_TestEcho2) {}
