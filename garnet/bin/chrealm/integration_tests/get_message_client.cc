// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/testing/chrealm/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/channel.h>
#include <zircon/status.h>
#include <zircon/types.h>

int main(int argc, const char** argv) {
  if (argc != 1) {
    fprintf(stderr, "Usage: %s\n", argv[0]);
    return 1;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto startup_context = sys::ComponentContext::Create();
  fuchsia::testing::chrealm::TestServicePtr test_svc;
  startup_context->svc()->Connect(test_svc.NewRequest());

  test_svc->GetMessage([&loop](fidl::StringPtr msg) {
    printf("%s", msg->c_str());
    loop.Quit();
  });
  loop.Run();

  return 0;
}
