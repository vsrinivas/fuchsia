// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/app/cpp/startup_context.h>
#include <lib/app_driver/cpp/app_driver.h>
#include <lib/async-loop/cpp/loop.h>

#include "peridot/examples/swap_cpp/module.h"

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  auto context = fuchsia::sys::StartupContext::CreateFromStartupInfo();
  modular::AppDriver<modular_example::ModuleApp> driver(
      context->outgoing().deprecated_services(),
      std::make_unique<modular_example::ModuleApp>(
          context.get(),
          [](auto view_manager, auto view_owner_request) {
            return new modular_example::ModuleView(
                std::move(view_manager), std::move(view_owner_request),
                0xFF00FFFF);
          }),
      [&loop] { loop.Quit(); });

  loop.Run();
  return 0;
}
