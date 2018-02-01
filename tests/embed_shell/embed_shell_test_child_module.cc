// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app_driver/cpp/module_driver.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/module/fidl/module.fidl.h"
#include "lib/ui/views/fidl/view_token.fidl.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"

using modular::testing::TestPoint;

namespace {

constexpr char kChildModuleName[] = "child";
constexpr char kChildModuleUrl[] =
    "file:///system/test/modular_tests/common_null_module";

class ChildApp : modular::ModuleWatcher {
 public:
  ChildApp(
      modular::ModuleHost* const module_host,
      fidl::InterfaceRequest<mozart::ViewProvider> /*view_provider_request*/,
      fidl::InterfaceRequest<app::ServiceProvider> /*outgoing_services*/)
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
    module_host_->module_context()->StartModuleInShell(
        kChildModuleName, kChildModuleUrl, nullptr /* link_name */,
        nullptr /* incoming_services */, child_module_.NewRequest(),
        nullptr /* surface_relation */, true /* focused */);

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
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  auto app_context = app::ApplicationContext::CreateFromStartupInfo();
  modular::ModuleDriver<ChildApp> driver(app_context.get(),
                                         [&loop] { loop.QuitNow(); });
  loop.Run();
  return 0;
}
