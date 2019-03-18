// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/app_driver/cpp/module_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fsl/vmo/strings.h>

#include "peridot/public/lib/integration_testing/cpp/reporting.h"
#include "peridot/public/lib/integration_testing/cpp/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/intents/defs.h"

using modular::testing::Signal;
using modular::testing::TestPoint;

namespace {

// Cf. README.md for what this test does and how.
class TestModule : fuchsia::modular::IntentHandler {
 public:
  TestModule(modular::ModuleHost* module_host,
             fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider>
                 view_provider_request)
      : app_view_provider_(std::move(view_provider_request)) {
    modular::testing::Init(module_host->startup_context(), __FILE__);
    module_host->startup_context()
        ->outgoing()
        .AddPublicService<fuchsia::modular::IntentHandler>(
            [this](fidl::InterfaceRequest<fuchsia::modular::IntentHandler>
                       request) {
              bindings_.AddBinding(this, std::move(request));
            });
  }

  // Called from ModuleDriver.
  void Terminate(fit::function<void()> done) {
    modular::testing::Done(std::move(done));
  }

 private:
  // |IntentHandler|
  void HandleIntent(fuchsia::modular::Intent intent) override {
    for (const auto& parameter : *intent.parameters) {
      if (parameter.data.is_json()) {
        if (parameter.name == kIntentParameterName ||
            parameter.name == kIntentParameterNameAlternate) {
          std::string parameter_data;
          FXL_CHECK(fsl::StringFromVmo(parameter.data.json(), &parameter_data));
          Signal(kChildModuleHandledIntent + parameter_data);
        }
      }
    }
  }

  fidl::BindingSet<fuchsia::modular::IntentHandler> bindings_;
  // We keep the view provider around so that story shell can hold a view for
  // us, but don't do anything with it.
  fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> app_view_provider_;

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
