// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START example_snippet]
#include <fidl/examples/routing/echo/cpp/fidl.h>
#include <lib/fidl/cpp/string.h>
#include <lib/sys/cpp/component_context.h>

#include <string>

#include <gtest/gtest.h>

TEST(EchoIntegrationTest, TestEcho) {
  ::fidl::examples::routing::echo::EchoSyncPtr echo_proxy;
  auto context = sys::ComponentContext::Create();
  context->svc()->Connect(echo_proxy.NewRequest());

  ::fidl::StringPtr request("Hello, world!");
  ::fidl::StringPtr response = nullptr;
  ASSERT_TRUE(echo_proxy->EchoString(request, &response) == ZX_OK);
  ASSERT_TRUE(request == response);
}
// [END example_snippet]
