// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <sdk/lib/sys/cpp/component_context.h>

#include "src/modular/lib/app_driver/cpp/module_driver.h"
#include "src/modular/lib/integration_testing/cpp/testing.h"

namespace {

// This module is launched with a specific runner specified in its .cmx.
class ModuleWithFakeRunner {
 public:
  ModuleWithFakeRunner(modular::ModuleHost* const module_host,
                       fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> view_provider_request)
      : view_provider_(std::move(view_provider_request)) {}

  // Called by ModuleDriver.
  void Terminate(fit::function<void()> done) { done(); }

 private:
  // We keep the view provider around so that story shell can hold a view for
  // us, but don't do anything with it.
  fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> view_provider_;
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();
  modular::ModuleDriver<ModuleWithFakeRunner> driver(context.get(), [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
