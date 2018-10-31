// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <lib/app_driver/cpp/module_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/callback/scoped_callback.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/memory/weak_ptr.h>

#include "peridot/public/lib/integration_testing/cpp/reporting.h"
#include "peridot/public/lib/integration_testing/cpp/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/parent_child/defs.h"

using modular::testing::Await;
using modular::testing::Get;
using modular::testing::Put;
using modular::testing::Signal;
using modular::testing::TestPoint;

namespace {

void StartModuleWithHandler(
    fuchsia::modular::ModuleContext* const module_context,
    fidl::InterfaceRequest<fuchsia::modular::ModuleController> request,
    fidl::StringPtr handler) {
  fuchsia::modular::Intent intent;
  intent.handler = handler;
  intent.action = kChildModuleAction;
  module_context->AddModuleToStory(
      kChildModuleName, std::move(intent), std::move(request), nullptr,
      [](const fuchsia::modular::StartModuleStatus) {});
}

// Cf. README.md for what this test does and how.
class TestApp {
 public:
  TestPoint initialized_{"Parent module initialized"};

  TestApp(modular::ModuleHost* module_host,
          fidl::InterfaceRequest<
              fuchsia::ui::viewsv1::ViewProvider> /*view_provider_request*/)
      : module_host_(module_host) {
    modular::testing::Init(module_host->startup_context(), __FILE__);
    initialized_.Pass();

    StartChildModuleTwice();
  }

  TestPoint stopped_{"Parent module stopped"};

  // Called by ModuleDriver.
  void Terminate(const std::function<void()>& done) {
    stopped_.Pass();
    modular::testing::Done(done);
  }

 private:
  TestPoint second_child_module_controller_closed_{
      "Second child module controller closed"};

  void StartChildModuleTwice() {
    StartModuleWithHandler(module_host_->module_context(),
                           child_module_.NewRequest(), kChildModuleUrl1);
    child_module_.set_error_handler([this] { OnFirstChildModuleStopped(); });

    // Once the module starts, start the same module again with the same
    // Intent, and then again but with a different Intent.handler. The second
    // call stops the previous module instance and starts a new one.
    Await("child_module_1_init", [this] {
      StartModuleWithHandler(module_host_->module_context(),
                             child_module_again_.NewRequest(),
                             kChildModuleUrl1);
      child_module_again_.set_error_handler(
          [this] { second_child_module_controller_closed_.Pass(); });
      StartModuleWithHandler(module_host_->module_context(),
                             child_module2_.NewRequest(), kChildModuleUrl2);
    });
  }

  TestPoint child_module1_stopped_{"Child module killed for restart"};

  void OnFirstChildModuleStopped() {
    child_module1_stopped_.Pass();

    // Confirm that the first module instance stopped, and then stop the second
    // module instance.
    Await("child_module_1_stop", [this] {
      Await("child_module_2_init", [this] {
        child_module2_->Stop([this] { OnChildModule2Stopped(); });
      });
    });
  }

  TestPoint child_module2_stopped_{"Second child module stopped"};

  void OnChildModule2Stopped() {
    child_module2_stopped_.Pass();
    Signal(modular::testing::kTestShutdown);
  }

  modular::ModuleHost* module_host_;
  fuchsia::modular::ModuleControllerPtr child_module_;
  fuchsia::modular::ModuleControllerPtr child_module_again_;
  fuchsia::modular::ModuleControllerPtr child_module2_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::ModuleDriver<TestApp> driver(context.get(),
                                        [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
