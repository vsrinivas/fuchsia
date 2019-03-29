// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/viewsv1token/cpp/fidl.h>
#include <lib/app_driver/cpp/module_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/callback/scoped_callback.h>
#include <src/lib/fxl/memory/weak_ptr.h>

#include "peridot/public/lib/integration_testing/cpp/reporting.h"
#include "peridot/public/lib/integration_testing/cpp/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/embed_shell/defs.h"

using modular::testing::Await;
using modular::testing::Signal;
using modular::testing::TestPoint;

namespace {

// Cf. README.md for what this test does and how.
class TestModule {
 public:
  TestModule(modular::ModuleHost* const module_host,
             fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider>
                 view_provider_request)
      : module_host_(module_host),
        view_provider_(std::move(view_provider_request)) {
    modular::testing::Init(module_host->startup_context(), __FILE__);
    ScheduleDone();
    StartChildModule();
  }

  void Terminate(fit::function<void()> done) {
    modular::testing::Done(std::move(done));
  }

 private:
  void ScheduleDone() {
    auto check = [this, done = std::make_shared<int>(0)] {
      ++*done;
      if (*done == 2) {
        Signal(kParentModuleDoneSignal);
      }
    };

    Await("story_shell_done", check);
    Await("child_module_done", check);
  }

  void StartChildModule() {
    fuchsia::modular::Intent intent;
    intent.action = kChildModuleAction;
    intent.handler = kChildModuleUrl;
    FXL_LOG(INFO) << "Starting child module = " << intent.handler;
    module_host_->module_context()->EmbedModule(
        kChildModuleName, std::move(intent), child_module_.NewRequest(),
        child_view_.NewRequest(),
        [](const fuchsia::modular::StartModuleStatus status) {
          FXL_LOG(INFO) << "StartModuleStatus="
                        << static_cast<uint32_t>(status);
        });
  }

  modular::ModuleHost* const module_host_;
  fuchsia::modular::ModuleControllerPtr child_module_;
  fuchsia::ui::viewsv1token::ViewOwnerPtr child_view_;

  // We keep the view provider around so that story shell can hold a view for
  // us, but don't do anything with it.
  fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> view_provider_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestModule);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = component::StartupContext::CreateFromStartupInfo();
  FXL_LOG(INFO) << "embed_shell_test_parent_module";
  modular::ModuleDriver<TestModule> driver(context.get(),
                                           [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
