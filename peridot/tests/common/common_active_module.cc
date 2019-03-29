// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/app_driver/cpp/module_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <src/lib/fxl/memory/weak_ptr.h>

#include "peridot/public/lib/integration_testing/cpp/reporting.h"
#include "peridot/public/lib/integration_testing/cpp/testing.h"
#include "peridot/tests/common/defs.h"

using modular::testing::Signal;
using modular::testing::TestPoint;

namespace {

// The ActiveModule issues Active() calls until it's terminated.
class ActiveModule {
 public:
  TestPoint initialized_{"Active module initialized"};

  ActiveModule(modular::ModuleHost* const module_host,
               fidl::InterfaceRequest<
                   fuchsia::ui::app::ViewProvider> /*view_provider_request*/)
      : module_host_(module_host), weak_ptr_factory_(this) {
    modular::testing::Init(module_host_->startup_context(), __FILE__);
    initialized_.Pass();
    Signal(kCommonActiveModuleStarted);

    ScheduleActive();
  }

  void ScheduleActive() {
    async::PostDelayedTask(
        async_get_default_dispatcher(),
        [this, weak_this = weak_ptr_factory_.GetWeakPtr()] {
          if (!weak_this) {
            return;
          }
          module_host_->module_context()->Active();
          Signal(kCommonActiveModuleOngoing);
          ScheduleActive();
        },
        zx::duration(ZX_SEC(1)));
  }

  // Called by ModuleDriver.
  TestPoint stopped_{"Active module stopped"};
  void Terminate(fit::function<void()> done) {
    Signal(kCommonActiveModuleStopped);
    stopped_.Pass();
    modular::testing::Done(std::move(done));
  }

 private:
  modular::ModuleHost* const module_host_;
  fxl::WeakPtrFactory<ActiveModule> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ActiveModule);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::ModuleDriver<ActiveModule> driver(context.get(),
                                             [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
