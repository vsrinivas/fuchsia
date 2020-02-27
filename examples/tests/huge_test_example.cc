// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>

#include <sstream>

#include "test_suite.h"

int main(int /*unused*/, const char** /*unused*/) {
  std::vector<example::TestInput> inputs;
  for (int i = 1; i <= 1000; i++) {
    std::ostringstream name;
    name << "FooTest" << i;
    example::TestInput ti = {.name = name.str(), .status = fuchsia::test::Status::PASSED};
    inputs.push_back(ti);
  }

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();

  example::TestSuite suite(&loop, std::move(inputs));
  context->outgoing()->AddPublicService(suite.GetHandler());

  loop.Run();
  return 0;
}
