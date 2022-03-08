// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>

#include "build_info.h"

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  std::shared_ptr<struct fake_info> info_ref = std::make_shared<struct fake_info>();

  BuildInfoTestControllerImpl test_controller_impl(info_ref);
  fidl::BindingSet<fuchsia::buildinfo::test::BuildInfoTestController> test_controller_bindings;
  context->outgoing()->AddPublicService(test_controller_bindings.GetHandler(&test_controller_impl));

  FakeProviderImpl provider_impl(info_ref);
  fidl::BindingSet<fuchsia::buildinfo::Provider> provider_bindings;
  context->outgoing()->AddPublicService(provider_bindings.GetHandler(&provider_impl));

  return loop.Run();
}
