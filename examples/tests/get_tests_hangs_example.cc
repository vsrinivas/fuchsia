// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/cpp/component_context.h>

#include "test_suite.h"

int main(int /*unused*/, const char** /*unused*/) {
  std::vector<example::TestInput> inputs = {
      {.name = "Example.Test1", .status = fuchsia::test::Status::PASSED},
      {.name = "Example.Test2", .status = fuchsia::test::Status::PASSED},
      {.name = "Example.Test3", .status = fuchsia::test::Status::PASSED}};

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = sys::ComponentContext::Create();

  example::TestSuite suite(&loop, std::move(inputs),
                           example::Options{.dont_service_get_tests = true});
  context->outgoing()->AddPublicService(suite.GetHandler());

  loop.Run();
  return 0;
}
