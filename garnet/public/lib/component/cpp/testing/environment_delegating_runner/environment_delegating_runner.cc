// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fxl/logging.h>

int main() {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto startup_ctx = component::StartupContext::CreateFromStartupInfo();
  auto env_runner =
      startup_ctx->ConnectToEnvironmentService<fuchsia::sys::Runner>();
  env_runner.set_error_handler([](zx_status_t) {
    // This program dies here to prevent proxying any further calls from our
    // own environment runner implementation.
    FXL_CHECK(false)
        << "Lost connection to the environment's fuchsia.sys.Runner";
  });

  fidl::BindingSet<fuchsia::sys::Runner> runner_bindings;
  startup_ctx->outgoing().AddPublicService(
      runner_bindings.GetHandler(env_runner.get()));

  loop.Run();
  return 0;
}
