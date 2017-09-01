// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/testing/component_base.h"
#include "apps/modular/lib/testing/reporting.h"
#include "apps/modular/lib/testing/testing.h"
#include "apps/modular/services/module/module.fidl.h"
#include "apps/modular/services/story/link.fidl.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "lib/ftl/tasks/task_runner.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/mtl/tasks/message_loop.h"

using modular::testing::TestPoint;

namespace {

constexpr char kLink[] = "context_link";

class TestApp : modular::testing::ComponentBase<modular::Module> {
 public:
  static void New() {
    new TestApp;  // deleted in Stop()
  }

 private:
  TestApp() { TestInit(__FILE__); }
  ~TestApp() override = default;

  TestPoint initialized_{"Child module initialized"};

  // |Module|
  void Initialize(
      fidl::InterfaceHandle<modular::ModuleContext> module_context,
      fidl::InterfaceHandle<app::ServiceProvider> /*incoming_services*/,
      fidl::InterfaceRequest<app::ServiceProvider> /*outgoing_services*/)
      override {
    module_context_.Bind(std::move(module_context));
    initialized_.Pass();

    module_context_->GetLink(kLink, link_.NewRequest());

    Set1();
  }

  void Set1() {
    link_->Set(nullptr,
               "{\"link_value\":\"1\",\"@context\":{\"topic\":\"/"
               "context_link_test\"}}");

    // TODO(mesch): If we set values on a Link too fast, they get swallowed by
    // syncing old values back from the ledger. FW-208.
    link_->Sync([this] {
      mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
          [this] { Set2(); }, ftl::TimeDelta::FromSeconds(5));
    });
  }

  void Set2() {
    link_->Set(nullptr,
               "{\"link_value\":\"2\",\"@context\":{\"topic\":\"/"
               "context_link_test\"}}");
  }

  // |Lifecycle|
  void Terminate() override {
    stopped_.Pass();
    DeleteAndQuitAndUnbind();
  }

  TestPoint stopped_{"Child module stopped"};

  modular::ModuleContextPtr module_context_;
  modular::LinkPtr link_;
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  mtl::MessageLoop loop;
  TestApp::New();
  loop.Run();
  return 0;
}
