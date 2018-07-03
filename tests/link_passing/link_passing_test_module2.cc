// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/views_v1/cpp/fidl.h>
#include <lib/app_driver/cpp/module_driver.h>
#include <lib/async-loop/cpp/loop.h>

#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/link_passing/defs.h"

using modular::testing::TestPoint;

namespace {

// Cf. README.md for what this test does and how.
class TestApp : fuchsia::modular::LinkWatcher {
 public:
  TestApp(modular::ModuleHost* const module_host,
          fidl::InterfaceRequest<
              fuchsia::ui::views_v1::ViewProvider> /*view_provider_request*/)
      : module_host_(module_host),
        link1_watcher_binding_(this),
        link2_watcher_binding_(this) {
    modular::testing::Init(module_host->startup_context(), __FILE__);
    modular::testing::GetStore()->Put("module2_init", "", [] {});

    Start();
  }

  void Start() {
    module_host_->module_context()->GetLink("link", link1_.NewRequest());
    link1_->WatchAll(link1_watcher_binding_.NewBinding());

    module_host_->module_context()->GetLink(nullptr, link2_.NewRequest());
    link2_->WatchAll(link2_watcher_binding_.NewBinding());

    fuchsia::modular::IntentParameter param1;
    param1.name = "link";
    param1.data.set_link_name("link");

    fuchsia::modular::IntentParameter param2;
    param2.name = nullptr;
    param2.data.set_link_name(nullptr);

    fuchsia::modular::Intent intent;
    intent.action.handler = kModule3Url;
    intent.parameters.push_back(std::move(param1));
    intent.parameters.push_back(std::move(param2));

    module_host_->module_context()->StartModule(
        "three", std::move(intent), module_controller_.NewRequest(),
        nullptr /* surface_relation */,
        [](fuchsia::modular::StartModuleStatus) {});
  }

  // Called from ModuleDriver.
  void Terminate(const std::function<void()>& done) {
    modular::testing::GetStore()->Put("module2_stop", "", [] {});
    modular::testing::Done(done);
  }

 private:
  // |fuchsia::modular::LinkWatcher|
  void Notify(fidl::StringPtr json) override {
    FXL_LOG(INFO) << "module2 link: " << json;
  }

  modular::ModuleHost* const module_host_;
  fuchsia::modular::LinkPtr link1_;
  fidl::Binding<fuchsia::modular::LinkWatcher> link1_watcher_binding_;
  fuchsia::modular::LinkPtr link2_;
  fidl::Binding<fuchsia::modular::LinkWatcher> link2_watcher_binding_;
  fuchsia::modular::ModuleControllerPtr module_controller_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  auto context = fuchsia::sys::StartupContext::CreateFromStartupInfo();
  modular::ModuleDriver<TestApp> driver(context.get(),
                                        [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
