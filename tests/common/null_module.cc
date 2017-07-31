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

class NullModule : modular::testing::ComponentBase<modular::Module> {
 public:
  static void New() {
    new NullModule;  // deleted in Stop()
  }

 private:
  NullModule() { TestInit(__FILE__); }
  ~NullModule() override = default;

  // |Module|
  void Initialize(
      fidl::InterfaceHandle<modular::ModuleContext> module_context,
      fidl::InterfaceHandle<app::ServiceProvider> /*incoming_services*/,
      fidl::InterfaceRequest<app::ServiceProvider> /*outgoing_services*/)
      override {
    module_context_.Bind(std::move(module_context));
    module_context_->Ready();
    initialized_.Pass();
    module_context_->Done();
  }

  // |Module|
  void Stop(const StopCallback& done) override {
    stopped_.Pass();
    DeleteAndQuit(done);
  }

  modular::ModuleContextPtr module_context_;

  TestPoint initialized_{"Child module initialized"};
  TestPoint stopped_{"Child module stopped"};
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  mtl::MessageLoop loop;
  NullModule::New();
  loop.Run();
  return 0;
}
