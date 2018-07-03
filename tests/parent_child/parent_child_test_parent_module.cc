// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/views_v1/cpp/fidl.h>
#include <lib/app_driver/cpp/module_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/callback/scoped_callback.h>
#include <lib/fxl/memory/weak_ptr.h>

#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/parent_child/defs.h"

using modular::testing::Await;
using modular::testing::Get;
using modular::testing::Put;
using modular::testing::Signal;
using modular::testing::TestPoint;

namespace {

void StartModuleWithLinkMapping(
    fuchsia::modular::ModuleContext* const module_context,
    std::string link_name,
    fidl::InterfaceRequest<fuchsia::modular::ModuleController> request) {
  fuchsia::modular::Intent intent;
  intent.action.handler = kChildModuleUrl;
  fuchsia::modular::IntentParameter intent_parameter;
  intent_parameter.name = "link";
  intent_parameter.data = fuchsia::modular::IntentParameterData();
  intent_parameter.data.set_link_name(link_name);
  intent.parameters.push_back(std::move(intent_parameter));
  module_context->StartModule(kChildModuleName, std::move(intent),
                              std::move(request), nullptr,
                              [](const fuchsia::modular::StartModuleStatus) {});
}

// Cf. README.md for what this test does and how.
class TestApp {
 public:
  TestPoint initialized_{"Parent module initialized"};

  TestApp(modular::ModuleHost* module_host,
          fidl::InterfaceRequest<
              fuchsia::ui::views_v1::ViewProvider> /*view_provider_request*/)
      : module_host_(module_host) {
    modular::testing::Init(module_host->startup_context(), __FILE__);
    initialized_.Pass();

    // The child module uses this TestStore value to track how many times
    // it has been initialized.
    Put("child_module_init_count", "0");

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
    // We set the two links to different values ("1" and "2"). The child module
    // checks this: the first time it is started, it will be attached to
    // module1link, and expects to see "1" in its link. The second time, it
    // expects to see "2". If it sees anything else, there was a failure in
    // starting, or not, the module given the StartModule() request.
    //
    // Internally, the child module tracks how many times its instance was
    // started.
    module_host_->module_context()->GetLink("module1link", link1_.NewRequest());
    link1_->Set(nullptr, "1");
    module_host_->module_context()->GetLink("module2link", link2_.NewRequest());
    link2_->Set(nullptr, "2");

    StartModuleWithLinkMapping(module_host_->module_context(), "module1link",
                               child_module_.NewRequest());
    child_module_.set_error_handler([this] { OnFirstChildModuleStopped(); });

    // Once the module starts, start the same module again with the same
    // Intent, and then again but with a different link mapping. The second
    // call stops the previous module instance and starts a new one.
    Await("child_module_init_1", [this] {
      StartModuleWithLinkMapping(module_host_->module_context(), "module1link",
                                 child_module_again_.NewRequest());
      child_module_again_.set_error_handler(
          [this] { second_child_module_controller_closed_.Pass(); });
      StartModuleWithLinkMapping(module_host_->module_context(), "module2link",
                                 child_module2_.NewRequest());
    });
  }

  TestPoint child_module1_stopped_{"Child module killed for restart"};

  void OnFirstChildModuleStopped() {
    child_module1_stopped_.Pass();

    // Confirm that the first module instance stopped, and then stop the second
    // module instance.
    Await("child_module_stop", [this] {
      Await("child_module_init_2", [this] {
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

  fuchsia::modular::LinkPtr link1_;
  fuchsia::modular::LinkPtr link2_;
  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  auto context = fuchsia::sys::StartupContext::CreateFromStartupInfo();
  modular::ModuleDriver<TestApp> driver(context.get(),
                                        [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
