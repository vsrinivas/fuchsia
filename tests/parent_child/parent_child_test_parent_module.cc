// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/views_v1/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "lib/app_driver/cpp/module_driver.h"
#include "lib/callback/scoped_callback.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/parent_child/defs.h"

using fuchsia::modular::testing::Await;
using fuchsia::modular::testing::Signal;
using fuchsia::modular::testing::TestPoint;

namespace {

// Cf. README.md for what this test does and how.
class TestApp {
 public:
  TestPoint initialized_{"Parent module initialized"};

  TestApp(fuchsia::modular::ModuleHost* module_host,
          fidl::InterfaceRequest<
              fuchsia::ui::views_v1::ViewProvider> /*view_provider_request*/)
      : module_host_(module_host) {
    fuchsia::modular::testing::Init(module_host->startup_context(), __FILE__);
    initialized_.Pass();

    StartChildModuleTwice();
  }

  TestPoint stopped_{"Parent module stopped"};

  // Called by ModuleDriver.
  void Terminate(const std::function<void()>& done) {
    stopped_.Pass();
    fuchsia::modular::testing::Done(done);
  }

 private:
  void StartChildModuleTwice() {
    fuchsia::modular::Intent intent;
    intent.action.handler = kChildModuleUrl;
    fuchsia::modular::IntentParameter intent_parameter;
    intent_parameter.name = "link";
    intent_parameter.data = fuchsia::modular::IntentParameterData();
    intent_parameter.data.set_link_name("module1link");
    intent.parameters.push_back(std::move(intent_parameter));
    module_host_->module_context()->StartModule(
        kChildModuleName, std::move(intent), child_module_.NewRequest(),
        nullptr, [](const fuchsia::modular::StartModuleStatus) {});

    // Once the module starts, start the same module again, but with a different
    // link mapping. This stops the previous module instance and starts a new
    // one.
    Await("child_module_init", [this] {
      child_module_.set_error_handler([this] { OnChildModuleStopped(); });

      fuchsia::modular::Intent intent;
      intent.action.handler = kChildModuleUrl;
      fuchsia::modular::IntentParameter intent_parameter;
      intent_parameter.name = "link";
      intent_parameter.data = fuchsia::modular::IntentParameterData();
      intent_parameter.data.set_link_name("module2link");
      intent.parameters.push_back(std::move(intent_parameter));
      module_host_->module_context()->StartModule(
          kChildModuleName, std::move(intent), child_module2_.NewRequest(),
          nullptr, [](const fuchsia::modular::StartModuleStatus) {});
    });
  }

  TestPoint child_module_down_{"Child module killed for restart"};

  void OnChildModuleStopped() {
    child_module_down_.Pass();

    // Confirm that the first module instance stopped, and then stop the second
    // module instance.
    Await("child_module_stop", [this] {
      child_module2_->Stop([this] { OnChildModule2Stopped(); });
    });
  }

  TestPoint child_module_stopped_{"Child module stopped"};

  void OnChildModule2Stopped() {
    child_module_stopped_.Pass();
    Signal(fuchsia::modular::testing::kTestShutdown);
  }

  fuchsia::modular::ModuleHost* module_host_;
  fuchsia::modular::ModuleControllerPtr child_module_;
  fuchsia::modular::ModuleControllerPtr child_module2_;
  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  auto context = fuchsia::sys::StartupContext::CreateFromStartupInfo();
  fuchsia::modular::ModuleDriver<TestApp> driver(context.get(),
                                                 [&loop] { loop.QuitNow(); });
  loop.Run();
  return 0;
}
