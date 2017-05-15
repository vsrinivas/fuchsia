// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "apps/modular/lib/testing/component_base.h"
#include "apps/modular/lib/testing/reporting.h"
#include "apps/modular/lib/testing/testing.h"
#include "apps/modular/services/module/module.fidl.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "lib/mtl/tasks/message_loop.h"

using modular::testing::TestPoint;

namespace {

constexpr int kTimeoutMilliseconds = 5000;

constexpr char kChildModule[] =
    "file:///system/apps/modular_tests/child_module";

class ParentApp : modular::testing::ComponentBase<modular::Module> {
 public:
  static void New() {
    new ParentApp;  // deletes itself in Stop()
  }

 private:
  ParentApp() { TestInit(__FILE__); }
  ~ParentApp() override = default;

  // |Module|
  void Initialize(
      fidl::InterfaceHandle<modular::ModuleContext> module_context,
      fidl::InterfaceHandle<app::ServiceProvider> incoming_services,
      fidl::InterfaceRequest<app::ServiceProvider> outgoing_services) override {
    initialized_.Pass();
    module_context_.Bind(std::move(module_context));

    StartModule(kChildModule);

    modular::testing::GetStore()->Get("child_module_init",
                                      [this](const fidl::String&) {
                                        child_module_->Stop([this] {
                                          callback_.Pass();
                                          module_context_->Done();
                                        });
                                      });

    // Start a timer to quit in case another test component misbehaves and we
    // time out.
    mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        Protect([this] { DeleteAndQuit([] {}); }),
        ftl::TimeDelta::FromMilliseconds(kTimeoutMilliseconds));
  }

  // |Module|
  void Stop(const StopCallback& done) override {
    stopped_.Pass();
    DeleteAndQuit(done);
  }

  void StartModule(const std::string& module_query) {
    fidl::InterfaceHandle<mozart::ViewOwner> module_view;
    constexpr char kChildLink[] = "child";
    constexpr char kChildModuleName[] = "child";
    module_context_->StartModule(kChildModuleName, module_query, kChildLink,
                                 nullptr, nullptr, child_module_.NewRequest(),
                                 module_view.NewRequest());
  }

  modular::ModuleContextPtr module_context_;
  modular::ModuleControllerPtr child_module_;

  TestPoint initialized_{"Parent module initialized"};
  TestPoint callback_{"Stop child callback invoked"};
  TestPoint stopped_{"Parent module stopped"};
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  ParentApp::New();
  loop.Run();
  return 0;
}
