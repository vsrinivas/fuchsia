// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/connect.h"
#include "apps/modular/lib/testing/component_base.h"
#include "apps/modular/lib/testing/reporting.h"
#include "apps/modular/lib/testing/testing.h"
#include "apps/modular/services/component/component_context.fidl.h"
#include "apps/modular/services/module/module.fidl.h"
#include "apps/modular/services/module/module_context.fidl.h"
#include "apps/modular/services/remote/remote_invoker.fidl.h"
#include "apps/modular/services/user/device_map.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/mtl/tasks/message_loop.h"

using modular::testing::TestPoint;

namespace {

// This is how long we wait for the test to finish before we timeout and tear
// down our test.
constexpr int kTimeoutMilliseconds = 10000;

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
    module_context_.Bind(std::move(module_context));
    initialized_.Pass();

    // Start a timer to quit in case the test agent misbehaves and we time out.
    mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        [this, ptr = GetWeakPtr()] {
          if (ptr) {
            DeleteAndQuit([]{});
          }
        }, ftl::TimeDelta::FromMilliseconds(kTimeoutMilliseconds));

    remote_invoker_ =
        application_context()
            ->ConnectToEnvironmentService<modular::RemoteInvoker>();
    remote_invoker_connected_.Pass();

    remote_invoker_->StartOnDevice(
        "test1", "test2", [this](fidl::String page_id) {
          if (page_id.get().empty()) {
            FTL_LOG(INFO) << "Failed to send rehydrate";
          } else {
            FTL_LOG(INFO) << "Sent rehydrate to page " << page_id;
            rehydrate_story_called_.Pass();
          }
          module_context_->Done();
        });
  }

  // |Module|
  void Stop(const StopCallback& done) override {
    stopped_.Pass();
    DeleteAndQuit(done);
  }

  modular::ModuleContextPtr module_context_;
  app::ServiceProviderPtr incoming_services_;
  app::ServiceProviderPtr outgoing_services_;
  modular::ComponentContextPtr component_context_;
  modular::RemoteInvokerPtr remote_invoker_;

  TestPoint initialized_{"Remote service test module initialized"};
  TestPoint remote_invoker_connected_{"Remote service connected"};
  TestPoint rehydrate_story_called_{"Rehydrate story called"};
  TestPoint stopped_{"Remote service test module stopped"};
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  ParentApp::New();
  loop.Run();
  return 0;
}
