// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app_driver/cpp/module_driver.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/module/fidl/module.fidl.h"
#include "lib/story/fidl/link.fidl.h"
#include "lib/ui/views/fidl/view_token.fidl.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"

using modular::testing::TestPoint;

namespace {

class TestApp {
 public:
  TestApp(
      modular::ModuleHost* module_host,
      fidl::InterfaceRequest<mozart::ViewProvider> /*view_provider_request*/,
      fidl::InterfaceRequest<app::ServiceProvider> /*outgoing_services*/) {
    modular::testing::Init(module_host->application_context(), __FILE__);
    initialized_.Pass();
    module_host->module_context()->GetLink("link1", link1_.NewRequest());
    module_host->module_context()->GetLink("link2", link2_.NewRequest());
    Set1();
  }

  // Called from ModuleDriver.
  void Terminate(const std::function<void()>& done) {
    stopped_.Pass();
    modular::testing::Done(done);
  }

 private:
  void Set1() {
    link1_->Set(nullptr, R"({"@type": "type1", "value": "value1"})");
    link2_->Set(nullptr,
                R"({"a_property": {"@type": "type2", "value": "value2"}})");
    // TODO(thatguy): When we have Entity support in ContextWriter, create a
    // simple Entity reference and slap it into the Link.
  }

  TestPoint initialized_{"Child module initialized"};
  TestPoint stopped_{"Child module stopped"};
  modular::LinkPtr link1_;
  modular::LinkPtr link2_;
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  auto app_context = app::ApplicationContext::CreateFromStartupInfo();
  modular::ModuleDriver<TestApp> driver(app_context.get(),
                                        [&loop] { loop.QuitNow(); });
  loop.Run();
  return 0;
}
