// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This component provides the |fuchsia.modular.testing.TestHarness| fidl
// service. This component will exit if the test harness becomes unavailable.

#include <lib/async-loop/cpp/loop.h>
#include <lib/modular_test_harness/cpp/test_harness_impl.h>

#include <memory>

#include <sdk/lib/sys/cpp/component_context.h>
#include <src/lib/fxl/logging.h>

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  auto context = sys::ComponentContext::Create();
  auto env = context->svc()->Connect<fuchsia::sys::Environment>();

  modular::testing::TestHarnessImpl test_harness_impl(env, [&loop] { loop.Quit(); });
  context->outgoing()->AddPublicService<fuchsia::modular::testing::TestHarness>(
      [&test_harness_impl](fidl::InterfaceRequest<fuchsia::modular::testing::TestHarness> request) {
        test_harness_impl.Bind(std::move(request));
      });

  modular::LifecycleImpl lifecycle_impl(context->outgoing(), &test_harness_impl);

  loop.Run();

  return 0;
}
