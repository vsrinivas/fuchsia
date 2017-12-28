// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "lib/app_driver/cpp/module_driver.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/module/fidl/module.fidl.h"
#include "lib/module/fidl/module_context.fidl.h"
#include "lib/ui/views/fidl/view_token.fidl.h"
#include "peridot/lib/callback/scoped_callback.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"

using modular::testing::TestPoint;

namespace {

constexpr char kChildModuleName[] = "child";
constexpr char kChildModule[] =
    "file:///system/test/modular_tests/embed_watcher_test_child_module";

class ParentApp : modular::EmbedModuleWatcher {
 public:
  ParentApp(
      modular::ModuleHost* module_host,
      fidl::InterfaceRequest<mozart::ViewProvider> /*view_provider_request*/,
      fidl::InterfaceRequest<app::ServiceProvider> /*outgoing_services*/)
      : module_host_(module_host), embed_module_watcher_binding_(this) {
    modular::testing::Init(module_host->application_context(), __FILE__);
    ScheduleDone();
    StartChildModule();
  }

  void Terminate(const std::function<void()>& done) {
    modular::testing::Done(done);
  }

 private:
  void ScheduleDone() {
    auto check = [this,
                  done = std::make_shared<int>(0)](const fidl::String& value) {
      ++*done;
      if (*done == 3) {
        module_host_->module_context()->Done();
      }
    };

    modular::testing::GetStore()->Get("story_shell_connect", check);
    modular::testing::GetStore()->Get("story_shell_focus", check);
    modular::testing::GetStore()->Get("child_module_done", check);
  }

  void StartChildModule() {
    module_host_->module_context()->EmbedModule(
        kChildModuleName, kChildModule, nullptr /* link_name */,
        nullptr /* incoming_services */, child_module_.NewRequest(),
        embed_module_watcher_binding_.NewBinding(), child_view_.NewRequest());
  }

  // |EmbedModuleWatcher|
  void OnStartModuleInShell(
      fidl::InterfaceHandle<modular::EmbedModuleController> controller)
      override {
    embed_module_controller_.Bind(std::move(controller));
    embed_module_controller_->Focus();
  }

  modular::ModuleHost* const module_host_;
  fidl::Binding<modular::EmbedModuleWatcher> embed_module_watcher_binding_;
  modular::ModuleControllerPtr child_module_;
  mozart::ViewOwnerPtr child_view_;
  modular::EmbedModuleControllerPtr embed_module_controller_;
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  auto app_context = app::ApplicationContext::CreateFromStartupInfo();
  modular::ModuleDriver<ParentApp> driver(app_context.get(),
                                          [&loop] { loop.QuitNow(); });
  loop.Run();
  return 0;
}
