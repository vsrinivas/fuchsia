// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/app_driver/cpp/module_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/callback/scoped_callback.h>
#include <lib/fsl/vmo/strings.h>
#include <src/lib/fxl/memory/weak_ptr.h>

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

void AddModuleToStoryWithHandler(
    fuchsia::modular::ModuleContext* const module_context,
    fidl::InterfaceRequest<fuchsia::modular::ModuleController> request,
    fidl::StringPtr handler) {
  fuchsia::modular::Intent intent;
  intent.handler = handler;
  intent.action = kChildModuleAction;
  module_context->AddModuleToStory(
      kChildModuleName, std::move(intent), std::move(request),
      nullptr /* surface_relation */,
      [](const fuchsia::modular::StartModuleStatus) {});
}

// Cf. README.md for what this test does and how.
class TestModule {
 public:
  TestPoint initialized_{"Parent module initialized"};

  TestModule(modular::ModuleHost* module_host,
             fidl::InterfaceRequest<
                 fuchsia::ui::app::ViewProvider> /*view_provider_request*/)
      : module_host_(module_host) {
    modular::testing::Init(module_host->startup_context(), __FILE__);
    initialized_.Pass();

    StartChildModuleTwice();
  }

  // Called by ModuleDriver.
  TestPoint stopped_{"Parent module stopped"};
  void Terminate(fit::function<void()> done) {
    stopped_.Pass();
    modular::testing::Done(std::move(done));
  }

 private:
  TestPoint second_child_module_controller_closed_{
      "Second child module controller closed"};

  void StartChildModuleTwice() {
    AddModuleToStoryWithHandler(module_host_->module_context(),
                                child_module_.NewRequest(), kChildModuleUrl1);
    child_module_.set_error_handler(
        [this](zx_status_t status) { OnFirstChildModuleStopped(); });

    // Once the module starts, start the same module again with the same
    // Intent, and then again but with a different Intent.handler. The second
    // call stops the previous module instance and starts a new one.
    Await("child_module_1_init", [this] {
      AddModuleToStoryWithHandler(module_host_->module_context(),
                                  child_module_again_.NewRequest(),
                                  kChildModuleUrl1);
      child_module_again_.set_error_handler([this](zx_status_t status) {
        second_child_module_controller_closed_.Pass();
      });
      AddModuleToStoryWithHandler(module_host_->module_context(),
                                  child_module2_.NewRequest(),
                                  kChildModuleUrl2);
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

    TestPipelinedAddAndStop();
  }

  TestPoint child_module_3_started_{"Third child module started"};
  TestPoint child_module_3_stopped_{"Third child module stopped"};
  void TestPipelinedAddAndStop() {
    // New test case: start a third child module, and immediately stop it.
    // We expect that the child module will go through its full lifecycle,
    // since we serialize these requests in sessionmgr.
    AddModuleToStoryWithHandler(module_host_->module_context(),
                                child_module3_.NewRequest(), kChildModuleUrl1);
    child_module3_->Stop([] { /* do nothing */ });
    child_module3_.events().OnStateChange =
        [this](fuchsia::modular::ModuleState module_state) {
          if (module_state == fuchsia::modular::ModuleState::RUNNING) {
            child_module_3_started_.Pass();
          }
          if (module_state == fuchsia::modular::ModuleState::STOPPED) {
            child_module_3_stopped_.Pass();
            Signal(modular::testing::kTestShutdown);
          }
        };
  }

  modular::ModuleHost* module_host_;
  fuchsia::modular::ModuleControllerPtr child_module_;
  fuchsia::modular::ModuleControllerPtr child_module_again_;
  fuchsia::modular::ModuleControllerPtr child_module2_;
  fuchsia::modular::ModuleControllerPtr child_module3_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestModule);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::ModuleDriver<TestModule> driver(context.get(),
                                           [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
