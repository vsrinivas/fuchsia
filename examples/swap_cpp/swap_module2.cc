// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app/cpp/application_context.h"
#include "lib/app_driver/cpp/app_driver.h"
#include "lib/fsl/tasks/message_loop.h"
#include "peridot/examples/swap_cpp/module.h"

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;

  auto app_context = app::ApplicationContext::CreateFromStartupInfo();
  modular::AppDriver<modular_example::ModuleApp> driver(
      app_context->outgoing_services(),
      std::make_unique<modular_example::ModuleApp>(
          app_context.get(),
          [](auto view_manager, auto view_owner_request) {
            return new modular_example::ModuleView(
                std::move(view_manager), std::move(view_owner_request), 0xFFFF00FF);
          }),
      [&loop] { loop.QuitNow(); });

  loop.Run();
  return 0;
}
