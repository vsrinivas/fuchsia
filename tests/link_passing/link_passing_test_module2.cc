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
#include "peridot/tests/link_passing/defs.h"

using modular::testing::TestPoint;

namespace {

// Cf. README.md for what this test does and how.
class TestApp : modular::LinkWatcher {
 public:
  TestApp(
      modular::ModuleHost* const module_host,
      fidl::InterfaceRequest<views_v1::ViewProvider> /*view_provider_request*/,
      fidl::InterfaceRequest<
      component::ServiceProvider> /*outgoing_services*/)
      : module_host_(module_host),
        link1_watcher_binding_(this),
        link2_watcher_binding_(this) {
    modular::testing::Init(module_host->application_context(), __FILE__);
    modular::testing::GetStore()->Put("module2_init", "", [] {});
    module_host_->module_context()->Ready();

    Start();
  }

  void Start() {
    module_host_->module_context()->GetLink("link", link1_.NewRequest());
    link1_->WatchAll(link1_watcher_binding_.NewBinding());

    module_host_->module_context()->GetLink(nullptr, link2_.NewRequest());
    link2_->WatchAll(link2_watcher_binding_.NewBinding());

    modular::IntentParameter param1;
    param1.name = "link";
    param1.data.set_link_name("link");

    modular::IntentParameter param2;
    param2.name = nullptr;
    param2.data.set_link_name(nullptr);

    modular::Intent intent;
    intent.action.handler = kModule3Url;
    intent.parameters.push_back(std::move(param1));
    intent.parameters.push_back(std::move(param2));

    module_host_->module_context()->StartModule(
        "three", std::move(intent),
        nullptr /* incoming_services */,
        module_controller_.NewRequest(),
        nullptr /* surface_relation */,
        [](modular::StartModuleStatus){});
  }

  // Called from ModuleDriver.
  void Terminate(const std::function<void()>& done) {
    modular::testing::GetStore()->Put("module2_stop", "", [] {});
    modular::testing::Done(done);
  }

 private:
  // |LinkWatcher|
  void Notify(fidl::StringPtr json) override {
    FXL_LOG(INFO) << "module2 link: " << json;
  }

  modular::ModuleHost* const module_host_;
  modular::LinkPtr link1_;
  fidl::Binding<modular::LinkWatcher> link1_watcher_binding_;
  modular::LinkPtr link2_;
  fidl::Binding<modular::LinkWatcher> link2_watcher_binding_;
  modular::ModuleControllerPtr module_controller_;

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
