// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <modular/cpp/fidl.h>
#include <views_v1/cpp/fidl.h>
#include "lib/app_driver/cpp/module_driver.h"
#include "lib/fsl/tasks/message_loop.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/embed_shell/defs.h"

using modular::testing::TestPoint;

namespace {

// Cf. README.md for what this test does and how.
class TestApp : modular::ModuleWatcher {
 public:
  TestApp(
      modular::ModuleHost* const module_host,
      fidl::InterfaceRequest<views_v1::ViewProvider> /*view_provider_request*/)
      : module_host_(module_host) {
    modular::testing::Init(module_host->application_context(), __FILE__);
    StartChildModule();
  }

  // Called from ModuleDriver.
  void Terminate(const std::function<void()>& done) {
    modular::testing::Done(done);
  }

 private:
  void StartChildModule() {
    modular::Intent intent;
    intent.action.handler = kCommonNullModule;
    module_host_->module_context()->StartModule(
        kChildModuleName, std::move(intent), nullptr /* incoming_services */,
        child_module_.NewRequest(), nullptr /* surface_relation */,
        [](const modular::StartModuleStatus) {});

    child_module_->Watch(module_watcher_.AddBinding(this));
  }

  // |ModuleWatcher|
  void OnStateChange(modular::ModuleState state) override {
    if (state == modular::ModuleState::RUNNING) {
      modular::testing::GetStore()->Put("child_module_done", "1", [] {});
    }
  }

  modular::ModuleHost* const module_host_;
  modular::ModuleControllerPtr child_module_;
  fidl::BindingSet<modular::ModuleWatcher> module_watcher_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  auto app_context = component::ApplicationContext::CreateFromStartupInfo();
  modular::ModuleDriver<TestApp> driver(app_context.get(),
                                        [&loop] { loop.QuitNow(); });
  loop.Run();
  return 0;
}
