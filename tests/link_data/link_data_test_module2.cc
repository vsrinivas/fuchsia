// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <lib/app_driver/cpp/module_driver.h>
#include <lib/async-loop/cpp/loop.h>

#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"

namespace {

// Cf. README.md for what this test does and how.
class TestApp : fuchsia::modular::LinkWatcher {
 public:
  TestApp(modular::ModuleHost* const module_host,
          fidl::InterfaceRequest<
              fuchsia::ui::viewsv1::ViewProvider> /*view_provider_request*/)
      : module_host_(module_host), link_watcher_binding_(this) {
    modular::testing::Init(module_host->startup_context(), __FILE__);
    modular::testing::GetStore()->Put("module2_init", "", [] {});
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
  // |fuchsia::modular::LinkWatcher|
  void Notify(fidl::StringPtr json) override {
    modular::testing::GetStore()->Put("module2_link", json, [] {});
  }

  modular::ModuleHost* const module_host_;
  fuchsia::modular::LinkPtr link_;
  fidl::Binding<fuchsia::modular::LinkWatcher> link_watcher_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::ModuleDriver<TestApp> driver(context.get(),
                                        [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
