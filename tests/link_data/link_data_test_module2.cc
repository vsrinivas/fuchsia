// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <modular/cpp/fidl.h>
#include <views_v1/cpp/fidl.h>
#include "lib/app_driver/cpp/module_driver.h"
#include "lib/fsl/tasks/message_loop.h"
#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"

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
        link_watcher_binding_(this) {
    modular::testing::Init(module_host->application_context(), __FILE__);
    modular::testing::GetStore()->Put("module2_init", "", [] {});
    module_host_->module_context()->Ready();
    Start();
  }

  void Start() {
    module_host_->module_context()->GetLink("link", link_.NewRequest());
    link_->WatchAll(link_watcher_binding_.NewBinding());
  }

  // Called from ModuleDriver.
  void Terminate(const std::function<void()>& done) {
    modular::testing::GetStore()->Put("module2_stop", "", [] {});
    modular::testing::Done(done);
  }

 private:
  // |LinkWatcher|
  void Notify(fidl::StringPtr json) override {
    modular::testing::GetStore()->Put("module2_link", json, [] {});
  }

  modular::ModuleHost* const module_host_;
  modular::LinkPtr link_;
  fidl::Binding<modular::LinkWatcher> link_watcher_binding_;

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
