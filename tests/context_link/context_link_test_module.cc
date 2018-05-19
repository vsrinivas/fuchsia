// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <modular/cpp/fidl.h>
#include <views_v1/cpp/fidl.h>
#include "lib/app_driver/cpp/module_driver.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/time/time_delta.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/context_link/defs.h"

using modular::testing::TestPoint;

namespace {

// Cf. README.md for what this test does and how.
class TestApp {
 public:
  TestPoint initialized_{"Child module initialized"};

  TestApp(
      modular::ModuleHost* module_host,
      fidl::InterfaceRequest<views_v1::ViewProvider> /*view_provider_request*/) {
    modular::testing::Init(module_host->application_context(), __FILE__);
    initialized_.Pass();
    module_host->module_context()->GetLink(kLink, link_.NewRequest());
    Set1();
  }

  TestPoint stopped_{"Child module stopped"};

  // Called from ModuleDriver.
  void Terminate(const std::function<void()>& done) {
    stopped_.Pass();
    modular::testing::Done(done);
  }

 private:
  void Set1() {
    link_->Set(nullptr,
               "{\"link_value\":\"1\",\"@context\":{\"topic\":\""
               "context_link_test\"}}");
    link_->Sync([this] { Set2(); });
  }

  void Set2() {
    link_->Set(nullptr,
               "{\"link_value\":\"2\",\"@context\":{\"topic\":\""
               "context_link_test\"}}");
  }

  modular::LinkPtr link_;

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
