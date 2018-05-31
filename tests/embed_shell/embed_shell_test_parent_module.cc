// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/views_v1/cpp/fidl.h>
#include <fuchsia/ui/views_v1_token/cpp/fidl.h>
#include "lib/app_driver/cpp/module_driver.h"
#include "lib/callback/scoped_callback.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/embed_shell/defs.h"

using fuchsia::modular::testing::Await;
using fuchsia::modular::testing::Signal;
using fuchsia::modular::testing::TestPoint;

namespace {

// Cf. README.md for what this test does and how.
class TestApp {
 public:
  TestApp(fuchsia::modular::ModuleHost* const module_host,
          fidl::InterfaceRequest<
              fuchsia::ui::views_v1::ViewProvider> /*view_provider_request*/)
      : module_host_(module_host) {
    fuchsia::modular::testing::Init(module_host->startup_context(), __FILE__);
    ScheduleDone();
    StartChildModule();
  }

  void Terminate(const std::function<void()>& done) {
    fuchsia::modular::testing::Done(done);
  }

 private:
  void ScheduleDone() {
    auto check = [this, done = std::make_shared<int>(0)] {
      ++*done;
      if (*done == 2) {
        Signal(fuchsia::modular::testing::kTestShutdown);
      }
    };

    Await("story_shell_done", check);
    Await("child_module_done", check);
  }

  void StartChildModule() {
    fuchsia::modular::Intent intent;
    intent.action.handler = kChildModuleUrl;
    module_host_->module_context()->EmbedModule(
        kChildModuleName, std::move(intent), child_module_.NewRequest(),
        child_view_.NewRequest(),
        [](const fuchsia::modular::StartModuleStatus) {});
  }

  fuchsia::modular::ModuleHost* const module_host_;
  fuchsia::modular::ModuleControllerPtr child_module_;
  fuchsia::ui::views_v1_token::ViewOwnerPtr child_view_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  auto context = component::StartupContext::CreateFromStartupInfo();
  fuchsia::modular::ModuleDriver<TestApp> driver(context.get(),
                                                 [&loop] { loop.QuitNow(); });
  loop.Run();
  return 0;
}
