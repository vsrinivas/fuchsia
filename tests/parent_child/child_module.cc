// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/tasks/message_loop.h"
#include "lib/module/fidl/module.fidl.h"
#include "lib/module_driver/cpp/module_driver.h"
#include "lib/ui/views/fidl/view_token.fidl.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"

using modular::testing::TestPoint;

namespace {

class ChildApp {
 public:
  ChildApp(modular::ModuleHost* module_host,
           fidl::InterfaceRequest<app::ServiceProvider> /*outgoing_services*/) {
    modular::testing::Init(module_host->application_context(), __FILE__);
    modular::testing::GetStore()->Put("child_module_init", "", [] {});
  }

  // Called from ModuleDriver.
  void Terminate(const std::function<void()>& done) {
    stopped_.Pass();
    modular::testing::GetStore()->Put("child_module_stop", "", [] {});
    modular::testing::Done(done);
  }

 private:
  TestPoint stopped_{"Child module stopped"};
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  auto app_context = app::ApplicationContext::CreateFromStartupInfo();
  modular::ModuleDriver<ChildApp> driver(app_context.get(),
                                         [&loop] { loop.QuitNow(); });
  loop.Run();
  return 0;
}
