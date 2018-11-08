// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <lib/app_driver/cpp/module_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fsl/vmo/strings.h>

#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/public/lib/integration_testing/cpp/reporting.h"
#include "peridot/public/lib/integration_testing/cpp/testing.h"
#include "peridot/tests/link_data/defs.h"

namespace {

// Cf. README.md for what this test does and how.
class TestModule : fuchsia::modular::LinkWatcher {
 public:
  TestModule(modular::ModuleHost* const module_host,
             fidl::InterfaceRequest<
                 fuchsia::ui::app::ViewProvider> /*view_provider_request*/)
      : module_host_(module_host), link_watcher_binding_(this) {
    modular::testing::Init(module_host->startup_context(), __FILE__);
    modular::testing::GetStore()->Put("module2_init", "", [] {});
    Start();
  }

  TestModule(modular::ModuleHost* const module_host,
             fidl::InterfaceRequest<
                 fuchsia::ui::viewsv1::ViewProvider> /*view_provider_request*/)
      : TestModule(
            module_host,
            fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider>(nullptr)) {}

  void Start() {
    module_host_->module_context()->GetLink(kModule2Link, link_.NewRequest());
    link_->WatchAll(link_watcher_binding_.NewBinding());
  }

  // Called from ModuleDriver.
  void Terminate(const std::function<void()>& done) {
    modular::testing::GetStore()->Put("module2_stop", "", [] {});
    modular::testing::Done(done);
  }

 private:
  // |fuchsia::modular::LinkWatcher|
  void Notify(fuchsia::mem::Buffer value) override {
    std::string json;
    FXL_CHECK(fsl::StringFromVmo(value, &json));
    modular::testing::GetStore()->Put("module2_link", json, [] {});
  }

  modular::ModuleHost* const module_host_;
  fuchsia::modular::LinkPtr link_;
  fidl::Binding<fuchsia::modular::LinkWatcher> link_watcher_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestModule);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::ModuleDriver<TestModule> driver(context.get(),
                                           [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
