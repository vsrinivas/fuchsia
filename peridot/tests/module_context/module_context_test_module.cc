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
#include "peridot/public/lib/integration_testing/cpp/reporting.h"
#include "peridot/public/lib/integration_testing/cpp/testing.h"
#include "peridot/tests/module_context/defs.h"

using ::modular::testing::Await;
using ::modular::testing::Signal;
using ::modular::testing::TestPoint;
using ::test::peridot::tests::trigger::TriggerTestServicePtr;

namespace {

// Cf. README.md for what this test does and how.
class TestModule {
 public:
  TestPoint initialized_{"Root module initialized"};

  TestModule(modular::ModuleHost* const module_host,
             fidl::InterfaceRequest<
                 fuchsia::ui::app::ViewProvider> /*view_provider_request*/) {
    modular::testing::Init(module_host->startup_context(), __FILE__);
    fuchsia::modular::ModuleContext* module_context =
        module_host->module_context();
    initialized_.Pass();

    module_host->module_context()->GetLink(kLinkName, link_.NewRequest());
    fidl::VectorPtr<std::string> name;
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

      Await(module_name_ == kFirstModuleName ? kFirstModuleCallDone
                                             : kSecondModuleCallDone,
            [module_context] { module_context->RemoveSelfFromStory(); });
      Await(module_name_ == kFirstModuleName ? kFirstModuleCallStartActivity
                                             : kSecondModuleCallStartActivity,
            [this, module_context] {
              module_context->StartOngoingActivity(
                  fuchsia::modular::OngoingActivityType::VIDEO,
                  ongoing_activity_.NewRequest());
            });
      Await(module_name_ == kFirstModuleName ? kFirstModuleCallStopActivity
                                             : kSecondModuleCallStopActivity,
            [this] { ongoing_activity_.Unbind(); });
    });
  }

  // Called by ModuleDriver.
  TestPoint stopped_{"Root module stopped"};
  void Terminate(fit::function<void()> done) {
    stopped_.Pass();
    std::string terminated = module_name_ == kFirstModuleName
                                 ? kFirstModuleTerminated
                                 : kSecondModuleTerminated;
    Signal(terminated);
    modular::testing::Done(std::move(done));
  }

 private:
  fuchsia::modular::LinkPtr link_;
  std::string module_name_ = "";
  fuchsia::modular::OngoingActivityPtr ongoing_activity_;

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
