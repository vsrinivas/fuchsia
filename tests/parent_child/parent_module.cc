// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "apps/modular/lib/testing/component_base.h"
#include "apps/modular/lib/testing/reporting.h"
#include "apps/modular/lib/testing/testing.h"
#include "apps/modular/services/module/module.fidl.h"
#include "lib/ui/views/fidl/view_token.fidl.h"
#include "lib/mtl/tasks/message_loop.h"

using modular::testing::TestPoint;

namespace {

constexpr int kTimeoutMilliseconds = 5000;

constexpr char kChildModuleName[] = "child";
constexpr char kChildModule[] =
    "file:///system/apps/modular_tests/child_module";

constexpr char kChildLink[] = "child";
constexpr char kChildLinkAlternate[] = "child2";

class ParentApp : modular::testing::ComponentBase<modular::Module> {
 public:
  static void New() {
    new ParentApp;  // deletes itself in Stop()
  }

 private:
  ParentApp() { TestInit(__FILE__); }
  ~ParentApp() override = default;

  TestPoint initialized_{"Parent module initialized"};

  // |Module|
  void Initialize(
      fidl::InterfaceHandle<modular::ModuleContext> module_context,
      fidl::InterfaceHandle<app::ServiceProvider> /*incoming_services*/,
      fidl::InterfaceRequest<app::ServiceProvider> /*outgoing_services*/)
      override {
    initialized_.Pass();
    module_context_.Bind(std::move(module_context));

    // Start a timer to quit in case another test component misbehaves and we
    // time out.
    mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        Protect([this] { DeleteAndQuit([] {}); }),
        ftl::TimeDelta::FromMilliseconds(kTimeoutMilliseconds));

    StartChildModuleTwice();
  }

  void StartChildModuleTwice() {
    module_context_->StartModuleInShell(
        kChildModuleName, kChildModule, kChildLink, nullptr, nullptr,
        child_module_.NewRequest(), nullptr, true);

    child_module_.set_connection_error_handler(
        [this] { OnChildModuleStopped(); });

    // Start the same module again, but with a different link. This stops the
    // previous module instance and starts a new one.
    module_context_->StartModuleInShell(
        kChildModuleName, kChildModule, kChildLinkAlternate, nullptr, nullptr,
        child_module2_.NewRequest(), nullptr, true);
  }

  TestPoint child_module_down_{"Child module killed for restart"};

  void OnChildModuleStopped() {
    child_module_down_.Pass();

    modular::testing::GetStore()->Get(
        "child_module_stop", [this](const fidl::String&) {
          child_module2_->Stop([this] { OnChildModule2Stopped(); });
        });
  }

  TestPoint child_module_stopped_{"Child module stopped"};

  void OnChildModule2Stopped() {
    child_module_stopped_.Pass();
    module_context_->Done();
  }

  TestPoint stopped_{"Parent module stopped"};

  // |Lifecycle|
  void Terminate() override {
    stopped_.Pass();
    DeleteAndQuitAndUnbind();
  }

  modular::ModuleContextPtr module_context_;
  modular::ModuleControllerPtr child_module_;
  modular::ModuleControllerPtr child_module2_;
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  mtl::MessageLoop loop;
  ParentApp::New();
  loop.Run();
  return 0;
}
