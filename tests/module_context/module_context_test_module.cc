// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/app_driver/cpp/module_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/component/cpp/connect.h>
#include <lib/fsl/vmo/strings.h>
#include <test/peridot/tests/trigger/cpp/fidl.h>

#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/module_context/defs.h"

using ::modular::testing::Await;
using ::modular::testing::Signal;
using ::modular::testing::TestPoint;
using ::test::peridot::tests::trigger::TriggerTestServicePtr;

namespace {

// Cf. README.md for what this test does and how.
class TestApp {
 public:
  TestPoint initialized_{"Root module initialized"};

  TestApp(modular::ModuleHost* const module_host,
          fidl::InterfaceRequest<
              fuchsia::ui::viewsv1::ViewProvider> /*view_provider_request*/) {
    modular::testing::Init(module_host->startup_context(), __FILE__);
    fuchsia::modular::ModuleContext* module_context =
        module_host->module_context();
    initialized_.Pass();

    module_host->module_context()->GetLink(kLinkName, link_.NewRequest());
    fidl::VectorPtr<fidl::StringPtr> name;
    name.push_back(kLinkKey);
    link_->Get(name.Clone(), [this, module_context](
                                 std::unique_ptr<fuchsia::mem::Buffer> value) {
      if (!value) {
        modular::testing::Fail("Did not receive a module name in link.");
        return;
      }

      std::string value_string;
      FXL_CHECK(fsl::StringFromVmo(*value, &value_string));

      rapidjson::Document document;
      document.Parse(value_string.c_str());
      module_name_ = document.GetString();

      Await(module_name_, [this, module_context] { module_context->Done(); });
    });
  }

  TestPoint stopped_{"Root module stopped"};
  // Called by ModuleDriver.
  void Terminate(const std::function<void()>& done) {
    stopped_.Pass();
    std::string terminated = module_name_ == kFirstModuleName
                                 ? kFirstModuleTerminated
                                 : kSecondModuleTerminated;
    Signal(terminated);
    modular::testing::Done(done);
  }

 private:
  fuchsia::modular::LinkPtr link_;
  std::string module_name_ = "";

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
