// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This component provides the |fuchsia.modular.testing.TestHarness| fidl
// service. This component will exit if the test harness becomes unavailable.

#include <lib/async-loop/cpp/loop.h>
#include <lib/modular_test_harness/cpp/test_harness_impl.h>
#include <sdk/lib/sys/cpp/component_context.h>
#include <src/lib/fxl/logging.h>

#include <memory>

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  std::unique_ptr<modular::testing::TestHarnessImpl> test_harness_impl;

  auto context = sys::ComponentContext::Create();
  auto env = context->svc()->Connect<fuchsia::sys::Environment>();
  context->outgoing()->AddPublicService<fuchsia::modular::testing::TestHarness>(
      [&loop, &context, &env, &test_harness_impl](
          fidl::InterfaceRequest<fuchsia::modular::testing::TestHarness>
              request) {
        test_harness_impl = std::make_unique<modular::testing::TestHarnessImpl>(
            env, std::move(request), [&loop] { loop.Quit(); });
      });

  loop.Run();
  return 0;
}
