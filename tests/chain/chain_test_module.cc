// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/views_v1/cpp/fidl.h>
#include <fuchsia/ui/views_v1_token/cpp/fidl.h>
#include <lib/app_driver/cpp/module_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fxl/functional/make_copyable.h>
#include <lib/fxl/time/time_delta.h>

#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/chain/defs.h"
#include "peridot/tests/common/defs.h"

using modular::testing::TestPoint;

namespace {

// Cf. README.md for what this test does and how.
class TestApp {
 public:
  TestPoint initialized_{"Parent module initialized"};

  TestApp(modular::ModuleHost* module_host,
          fidl::InterfaceRequest<
              fuchsia::ui::views_v1::ViewProvider> /*view_provider_request*/)
      : module_context_(module_host->module_context()) {
    module_context_->GetComponentContext(component_context_.NewRequest());
    modular::testing::Init(module_host->startup_context(), __FILE__);
    initialized_.Pass();

    // We'll use an fuchsia::modular::Entity stored on one of our Links, which
    // will be used in the resolution process to choose a compatible Module.
    // TODO(thatguy): We should be specifying type constraints when we create
    // the fuchsia::modular::Link.
    auto entity_data =
        fidl::VectorPtr<fuchsia::modular::TypeToDataEntry>::New(1);
    entity_data->at(0) = fuchsia::modular::TypeToDataEntry();
    entity_data->at(0).type = "myType";
    entity_data->at(0).data = "1337";

    component_context_->CreateEntityWithData(
        std::move(entity_data), [this](fidl::StringPtr reference) {
          entity_one_reference_ = reference;
          EmbedModule();
        });
  }

  TestPoint stopped_{"Parent module stopped"};

  // Called from ModuleDriver.
  void Terminate(const std::function<void()>& done) {
    stopped_.Pass();
    modular::testing::Done(done);
  }

 private:
  TestPoint start_intent_{"Started child Intent"};

  void EmbedModule() {
    intent_.handler = kChildModuleUrl;
    intent_.parameters.resize(4);

    // We'll put three parameters "one", "two" and "three" on the
    // fuchsia::modular::Intent.
    //
    // The first is used to match the Module, because we know that it expects
    // a parameter named "one". The other two are extra and are going to be
    // passed on to the Module regardless.
    //
    // The second parameter is set to a Link that we own with regular JSON
    // content.
    //
    // The third parameter we expect to reference a Link created on our behalf
    // by the Framework. We don't get access to that Link.
    module_context_->GetLink("foo", link_one_.NewRequest());
    link_one_->SetEntity(entity_one_reference_);
    fuchsia::modular::IntentParameterData parameter_data;
    parameter_data.set_link_name("foo");
    intent_.parameters->at(0) = fuchsia::modular::IntentParameter();
    intent_.parameters->at(0).name = "one";
    intent_.parameters->at(0).data = std::move(parameter_data);

    module_context_->GetLink("bar", link_two_.NewRequest());
    link_two_->Set(nullptr, "12345");
    parameter_data = fuchsia::modular::IntentParameterData();
    parameter_data.set_link_name("bar");
    intent_.parameters->at(1) = fuchsia::modular::IntentParameter();
    intent_.parameters->at(1).name = "two";
    intent_.parameters->at(1).data = std::move(parameter_data);

    parameter_data = fuchsia::modular::IntentParameterData();
    parameter_data.set_json("67890");
    intent_.parameters->at(2) = fuchsia::modular::IntentParameter();
    intent_.parameters->at(2).name = "three";
    intent_.parameters->at(2).data = std::move(parameter_data);

    // This noun doesn't have a name, and will appear as the root or default
    // link for the child mod. This is for backwards compatibility.
    parameter_data = fuchsia::modular::IntentParameterData();
    parameter_data.set_json("1337");
    intent_.parameters->at(3) = fuchsia::modular::IntentParameter();
    intent_.parameters->at(3).data = std::move(parameter_data);

    // Sync to avoid race conditions between writing
    link_one_->Sync([this] {
      link_two_->Sync([this] {
        module_context_->EmbedModule(
            "my child", std::move(intent_), child_module_.NewRequest(),
            child_view_.NewRequest(),
            [this](fuchsia::modular::StartModuleStatus status) {
              if (status == fuchsia::modular::StartModuleStatus::SUCCESS) {
                start_intent_.Pass();
              }
            });
      });
    });
  }

  fuchsia::modular::ComponentContextPtr component_context_;
  fuchsia::modular::ModuleContext* module_context_;
  fuchsia::modular::ModuleControllerPtr child_module_;
  fuchsia::ui::views_v1_token::ViewOwnerPtr child_view_;

  fidl::StringPtr entity_one_reference_;
  fuchsia::modular::Intent intent_;

  fuchsia::modular::LinkPtr link_one_;
  fuchsia::modular::LinkPtr link_two_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::ModuleDriver<TestApp> driver(context.get(),
                                        [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
