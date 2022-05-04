// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include <cstring>

#include "src/ui/a11y/testing/fake_a11y_manager.h"

namespace {

int run_a11y_manager(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  a11y_testing::FakeA11yManager fake_a11y_manager;
  context->outgoing()->AddPublicService(fake_a11y_manager.GetHandler());

  a11y_testing::FakeMagnifier fake_magnifier;
  context->outgoing()->AddPublicService(fake_magnifier.GetTestMagnifierHandler());
  context->outgoing()->AddPublicService(fake_magnifier.GetMagnifierHandler());

  loop.Run();
  return 0;
}

}  // namespace

int main(int argc, const char** argv) { return run_a11y_manager(argc, argv); }
