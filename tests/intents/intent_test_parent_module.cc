// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <lib/app_driver/cpp/module_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fsl/vmo/strings.h>

#include "peridot/public/lib/integration_testing/cpp/reporting.h"
#include "peridot/public/lib/integration_testing/cpp/testing.h"
#include "peridot/tests/common/defs.h"
#include "peridot/tests/intents/defs.h"

using modular::testing::Await;
using modular::testing::Signal;
using modular::testing::TestPoint;

namespace {

void StartModuleWithJsonParameter(
    fuchsia::modular::ModuleContext* const module_context,
    std::string module_name, std::string parameter_name,
    std::string parameter_content,
    fidl::InterfaceRequest<fuchsia::modular::ModuleController> request) {
  fuchsia::modular::Intent intent;
  intent.handler = kChildModuleUrl;
  intent.action = kChildModuleAction;

  {
    fuchsia::modular::IntentParameter intent_parameter;
    intent_parameter.name = parameter_name;
    intent_parameter.data = fuchsia::modular::IntentParameterData();
    fsl::SizedVmo vmo;
    FXL_CHECK(fsl::VmoFromString(parameter_content, &vmo));
    intent_parameter.data.set_json(std::move(vmo).ToTransport());
    intent.parameters.push_back(std::move(intent_parameter));
  }

  // This tests a null parameter name; it should be excluded from module
  // resolution altogether.
  {
    fuchsia::modular::IntentParameter intent_parameter;
    intent_parameter.name = nullptr;
    intent_parameter.data = fuchsia::modular::IntentParameterData();
    fsl::SizedVmo vmo;
    FXL_CHECK(fsl::VmoFromString(R"("")", &vmo));
    intent_parameter.data.set_json(std::move(vmo).ToTransport());
    intent.parameters.push_back(std::move(intent_parameter));
  }

  module_context->AddModuleToStory(
      module_name, std::move(intent), std::move(request), nullptr,
      [](const fuchsia::modular::StartModuleStatus) {});
}

// Cf. README.md for what this test does and how.
class TestModule {
 public:
  TestPoint initialized_{"Parent module initialized"};
  TestModule(modular::ModuleHost* module_host,
             fidl::InterfaceRequest<
                 fuchsia::ui::app::ViewProvider> /*view_provider_request*/)
      : module_host_(module_host) {
    modular::testing::Init(module_host->startup_context(), __FILE__);
    initialized_.Pass();
    TestStartWithModuleControllerRequest();
  }

  TestModule(modular::ModuleHost* const module_host,
             fidl::InterfaceRequest<
                 fuchsia::ui::viewsv1::ViewProvider> /*view_provider_request*/)
      : TestModule(
            module_host,
            fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider>(nullptr)) {}

  // Tests that a module which is started with an intent and exposes an intent
  // handler gets notified of the intent by the framework.
  TestPoint intent_was_handled_{"First intent was handled"};
  void TestStartWithModuleControllerRequest() {
    std::string json = "\"first\"";
    StartModuleWithJsonParameter(module_host_->module_context(),
                                 kChildModuleName, kIntentParameterName, json,
                                 child_module_.NewRequest());
    Await(kChildModuleHandledIntent + json, [this] {
      intent_was_handled_.Pass();
      TestStartSecondIntentSameParameter();
    });
  }

  // Tests that a second intent sent to an already running module with the same
  // parameters but different data notifies the intent handler of the new
  // intent.
  TestPoint second_intent_was_handled_{"Second intent was handled"};
  void TestStartSecondIntentSameParameter() {
    std::string json = "\"second\"";
    StartModuleWithJsonParameter(module_host_->module_context(),
                                 kChildModuleName, kIntentParameterName, json,
                                 child_module_second_.NewRequest());
    Await(kChildModuleHandledIntent + json, [this] {
      second_intent_was_handled_.Pass();
      TestStartThirdIntentDifferentParameter();
    });
  }

  // Tests that a third intent with different parameters is delivered to the
  // already running intent handler.
  TestPoint third_intent_was_handled_{"Third intent was handled"};
  void TestStartThirdIntentDifferentParameter() {
    std::string json = "\"third\"";
    StartModuleWithJsonParameter(
        module_host_->module_context(), kChildModuleName,
        kIntentParameterNameAlternate, json, child_module_second_.NewRequest());
    Await(kChildModuleHandledIntent + json, [this] {
      third_intent_was_handled_.Pass();
      TestStartIntentWithLinkNameAndPath();
    });
  }

  // Tests that a link_name parameter and a link_path parameter both get
  // modified by the framework to point to the appropriate link name when given
  // to the intent handler.
  TestPoint fourth_intent_was_handled_{"Fourth intent was handled"};
  void TestStartIntentWithLinkNameAndPath() {
    fuchsia::modular::Intent intent;
    intent.handler = kChildModuleUrl;
    intent.action = kChildModuleAction;
    // The framework populates the intent handler's link namespace and provides
    // each parameter in a link with the parameter name.
    std::string first_parameter_name = "first_param";
    std::string second_parameter_name = "second_param";

    fuchsia::modular::IntentParameter intent_parameter;
    intent_parameter.name = first_parameter_name;
    intent_parameter.data = fuchsia::modular::IntentParameterData();
    // The link name here should not be seen by the intent handler, but rather
    // the handler will be given the link name in its own namespace, in this
    // case first_parameter_name.
    intent_parameter.data.set_link_name("does_not_matter");

    fuchsia::modular::IntentParameter intent_parameter2;
    intent_parameter2.name = second_parameter_name;
    intent_parameter2.data = fuchsia::modular::IntentParameterData();
    fuchsia::modular::LinkPath path;
    // The link_path here should not be seen by the intent handler, but rather
    // the handler will be given the link name in its own namespace, in this
    // case first_parameter_name.
    path.link_name = "does_not_matter_either";
    std::vector<std::string> module_path;
    module_path.push_back("nor_does_this_matter");
    path.module_path = std::move(module_path);
    intent_parameter2.data.set_link_path(std::move(path));

    intent.parameters.push_back(std::move(intent_parameter));
    intent.parameters.push_back(std::move(intent_parameter2));

    module_host_->module_context()->AddModuleToStory(
        kChildModuleName, std::move(intent), child_module_second_.NewRequest(),
        nullptr, [](const fuchsia::modular::StartModuleStatus) {});

    // Verify that the link_name parameter was delivered as a link named the
    // same as the parameter.
    Await(kChildModuleHandledIntent + first_parameter_name,
          [this, second_parameter_name] {
            // Verify that the link_path parameter was delivered as a link named
            // the same as the parameter.
            Await(kChildModuleHandledIntent + second_parameter_name, [this] {
              fourth_intent_was_handled_.Pass();
              Signal(modular::testing::kTestShutdown);
            });
          });
  }

  // Called by ModuleDriver.
  TestPoint stopped_{"Parent module stopped"};
  void Terminate(const std::function<void()>& done) {
    stopped_.Pass();
    modular::testing::Done(done);
  }

 private:
  modular::ModuleHost* module_host_;
  fuchsia::modular::ModuleControllerPtr child_module_;
  fuchsia::modular::ModuleControllerPtr child_module_second_;

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
