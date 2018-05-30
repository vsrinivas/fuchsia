// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/views_v1/cpp/fidl.h>
#include "lib/app_driver/cpp/module_driver.h"
#include "lib/fsl/tasks/message_loop.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/link_passing/defs.h"

using fuchsia::modular::testing::TestPoint;

namespace {

// Cf. README.md for what this test does and how.
class TestApp : fuchsia::modular::LinkWatcher {
 public:
  TestApp(fuchsia::modular::ModuleHost* const module_host,
          fidl::InterfaceRequest<
              fuchsia::ui::views_v1::ViewProvider> /*view_provider_request*/)
      : module_host_(module_host),
        link1_watcher_binding_(this),
        link2_watcher_binding_(this) {
    fuchsia::modular::testing::Init(module_host->startup_context(), __FILE__);
    fuchsia::modular::testing::GetStore()->Put("module3_init", "", [] {});

    Start();
  }

  void Start() {
    module_host_->module_context()->GetLink("link", link1_.NewRequest());
    link1_->WatchAll(link1_watcher_binding_.NewBinding());

    module_host_->module_context()->GetLink(nullptr, link2_.NewRequest());
    link2_->WatchAll(link2_watcher_binding_.NewBinding());

    link1_->Set(nullptr, "1");
    link2_->Set(nullptr, "2");
  }

  // Called from ModuleDriver.
  void Terminate(const std::function<void()>& done) {
    fuchsia::modular::testing::GetStore()->Put("module3_stop", "", [] {});
    fuchsia::modular::testing::Done(done);
  }

 private:
  // |LinkWatcher|
  void Notify(fidl::StringPtr json) override {
    FXL_LOG(INFO) << "module3 link: " << json;
  }

  fuchsia::modular::ModuleHost* const module_host_;
  fuchsia::modular::LinkPtr link1_;
  fidl::Binding<fuchsia::modular::LinkWatcher> link1_watcher_binding_;
  fuchsia::modular::LinkPtr link2_;
  fidl::Binding<fuchsia::modular::LinkWatcher> link2_watcher_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  auto context = fuchsia::sys::StartupContext::CreateFromStartupInfo();
  fuchsia::modular::ModuleDriver<TestApp> driver(context.get(),
                                                 [&loop] { loop.QuitNow(); });
  loop.Run();
  return 0;
}
