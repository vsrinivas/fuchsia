// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/process.h>
#include <lib/zx/vmar.h>
#include <zircon/process.h>
#include <zircon/status.h>

#include "examples/tests/test_suite.h"

fuchsia::test::Status run_test() {
  zx::process process;
  zx::vmar root_vmar;

  const char* name = "raw_process";
  zx_status_t status =
      zx_process_create(zx_job_default(), name, static_cast<uint32_t>(strlen(name)), 0,
                        process.reset_and_get_address(), root_vmar.reset_and_get_address());
  if (status != ZX_OK) {
    printf("zx_process_create failed with %d (%s)\n", status, zx_status_get_string(status));
    return fuchsia::test::Status::FAILED;
  }

  zx::process shared_process;
  zx::vmar restricted_root_vmar;
  // The test process itself was created with ZX_PROCESS_SHARED, so use it as the shared process.
  status = zx_process_create_shared(zx_process_self(), 0, name, static_cast<uint32_t>(strlen(name)),
                                    shared_process.reset_and_get_address(),
                                    restricted_root_vmar.reset_and_get_address());
  if (status != ZX_OK) {
    printf("zx_proces_create_shared failed with %d (%s)\n", status, zx_status_get_string(status));
    return fuchsia::test::Status::FAILED;
  }

  return fuchsia::test::Status::PASSED;
}

int main(int argc, const char** argv) {
  std::vector<example::TestInput> inputs = {
      {.name = "CreateRawProcess.Smoke", .status = run_test()},
  };

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  example::TestSuite suite(&loop, std::move(inputs));
  context->outgoing()->AddPublicService(suite.GetHandler());

  loop.Run();
  return 0;
}
