// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/views_v1/cpp/fidl.h>
#include <lib/app_driver/cpp/module_driver.h>
#include <lib/async-loop/cpp/loop.h>

#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/embed_shell/defs.h"

using modular::testing::TestPoint;

namespace {

// Cf. README.md for what this test does and how.
class TestApp {
 public:
  TestApp(modular::ModuleHost* const module_host,
          fidl::InterfaceRequest<
              fuchsia::ui::views_v1::ViewProvider> /*view_provider_request*/)
      : module_host_(module_host) {
    modular::testing::Init(module_host->startup_context(), __FILE__);
    StartChildModule();
  }

  // Called from ModuleDriver.
  void Terminate(const std::function<void()>& done) {
    modular::testing::Done(done);
  }

 private:
  void StartChildModule() {
    fuchsia::modular::Intent intent;
    intent.handler = kCommonNullModule;

    child_module_.events().OnStateChange =
        [this](fuchsia::modular::ModuleState new_state) {
          OnStateChange(std::move(new_state));
        };

    module_host_->module_context()->StartModule(
        kChildModuleName, std::move(intent), child_module_.NewRequest(),
        nullptr /* surface_relation */,
        [](const fuchsia::modular::StartModuleStatus) {});
  }

  void OnStateChange(fuchsia::modular::ModuleState state) {
    if (state == fuchsia::modular::ModuleState::RUNNING) {
      modular::testing::GetStore()->Put("child_module_done", "1", [] {});
    }
  }

  modular::ModuleHost* const module_host_;
  fuchsia::modular::ModuleControllerPtr child_module_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::ModuleDriver<TestApp> driver(context.get(),
                                        [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
