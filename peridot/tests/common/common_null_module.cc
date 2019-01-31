// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <lib/app_driver/cpp/module_driver.h>
#include <lib/async-loop/cpp/loop.h>

#include "peridot/public/lib/integration_testing/cpp/reporting.h"
#include "peridot/public/lib/integration_testing/cpp/testing.h"
#include "peridot/tests/common/defs.h"

using modular::testing::Signal;
using modular::testing::TestPoint;

namespace {

// The NullModule just sits there and does nothing until it's terminated.
class NullModule {
 public:
  NullModule(modular::ModuleHost* const module_host,
             fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider>
                 view_provider_request)
      : module_host_(module_host),
        app_view_provider_(std::move(view_provider_request)) {
    modular::testing::Init(module_host_->startup_context(), __FILE__);
    initialized_.Pass();
    Signal(kCommonNullModuleStarted);
  }

  NullModule(modular::ModuleHost* const module_host,
             fidl::InterfaceRequest<fuchsia::ui::viewsv1::ViewProvider>
                 view_provider_request)
      : NullModule(
            module_host,
            fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider>(nullptr)) {
    viewsv1_view_provider_ = std::move(view_provider_request);
  }

  // Called by ModuleDriver.
  void Terminate(const std::function<void()>& done) {
    Signal(kCommonNullModuleStopped);
    stopped_.Pass();
    modular::testing::Done(done);
  }

 private:
  TestPoint initialized_{"Null module initialized"};
  TestPoint stopped_{"Null module stopped"};

  modular::ModuleHost* const module_host_;
  // We keep the view provider around so that story shell can hold a view for
  // us, but don't do anything with it.
  fidl::InterfaceRequest<fuchsia::ui::viewsv1::ViewProvider>
      viewsv1_view_provider_;
  fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> app_view_provider_;
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::ModuleDriver<NullModule> driver(context.get(),
                                           [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
