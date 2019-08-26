// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/trace-provider/provider.h>

#include "peridot/examples/swap_cpp/module.h"
#include "src/modular/lib/app_driver/cpp/app_driver.h"

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());

  auto context = sys::ComponentContext::Create();
  modular::AppDriver<modular_example::ModuleApp> driver(
      context->outgoing(),
      std::make_unique<modular_example::ModuleApp>(context.get(),
                                                   [](scenic::ViewContext view_context) {
                                                     return new modular_example::ModuleView(
                                                         std::move(view_context), 0xFFFF00FF);
                                                   }),
      [&loop] { loop.Quit(); });

  loop.Run();
  return 0;
}
