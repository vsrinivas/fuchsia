// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/status.h>
#include <zircon/syscalls-next.h>

#include "examples/tests/test_suite.h"

int main(int argc, const char** argv) {
  fuchsia::test::Status result = fuchsia::test::Status::PASSED;

  // If this program is not run with the "next" vDSO, then the dynamic linker
  // will fail to link the zx_syscall_next_1 symbol.
  zx_status_t status = zx_syscall_next_1(12);
  if (status != ZX_OK) {
    printf("zx_syscall_next_1 failed with %d (%s)\n", status, zx_status_get_string(status));
    result = fuchsia::test::Status::FAILED;
  }

  std::vector<example::TestInput> inputs = {
      {.name = "NextVDSO.Smoke", .status = result},
  };

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  example::TestSuite suite(&loop, std::move(inputs));
  context->outgoing()->AddPublicService(suite.GetHandler());

  loop.Run();
  return 0;
}
