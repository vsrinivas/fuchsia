// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/testing/component_base.h"
#include "apps/modular/lib/testing/reporting.h"
#include "apps/modular/lib/testing/testing.h"
#include "apps/modular/services/module/module.fidl.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "lib/mtl/tasks/message_loop.h"

using modular::testing::TestPoint;

namespace {

class ChildApp : modular::testing::ComponentBase<modular::Module> {
 public:
  static void New() {
    new ChildApp;  // deleted in Stop()
  }

 private:
  ChildApp() { TestInit(__FILE__); }
  ~ChildApp() override = default;

  // |Module|
  void Initialize(
      fidl::InterfaceHandle<modular::ModuleContext> module_context,
      fidl::InterfaceHandle<app::ServiceProvider> /*incoming_services*/,
      fidl::InterfaceRequest<app::ServiceProvider> /*outgoing_services*/)
      override {
    module_context_.Bind(std::move(module_context));
  }

  // |Lifecycle|
  void Terminate() override {
    stopped_.Pass();
    modular::testing::GetStore()->Put("child_module_stop", "", [] {});
    DeleteAndQuitAndUnbind();
  }

  modular::ModuleContextPtr module_context_;

  TestPoint stopped_{"Child module stopped"};
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  mtl::MessageLoop loop;
  ChildApp::New();
  loop.Run();
  return 0;
}
