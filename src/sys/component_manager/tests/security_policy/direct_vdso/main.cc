// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include "examples/tests/test_suite.h"

fuchsia::test::Status run_test() {
  zx::vmo vdso_vmo(zx_take_startup_handle(PA_HND(PA_VMO_VDSO, 1)));
  if (!vdso_vmo.is_valid()) {
    return fuchsia::test::Status::FAILED;
  }

  char name[ZX_MAX_NAME_LEN] = {};
  zx_status_t status = vdso_vmo.get_property(ZX_PROP_NAME, name, sizeof(name));
  if (status != ZX_OK || strcmp(name, "vdso/direct") != 0) {
    return fuchsia::test::Status::FAILED;
  }

  return fuchsia::test::Status::PASSED;
}

int main(int argc, const char** argv) {
  std::vector<example::TestInput> inputs = {
      {.name = "DirectVDSO.Smoke", .status = run_test()},
  };

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  example::TestSuite suite(&loop, std::move(inputs));
  context->outgoing()->AddPublicService(suite.GetHandler());

  loop.Run();
  return 0;
}
