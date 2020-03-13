// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/testing/cpp/fidl.h>

#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_module.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"

namespace {

// The name of the intent parameter which contains a string which the second
// module is to append to its signal.
constexpr char kIntentParameterName[] = "intent_parameter";

// The initial module's intent parameter data. This needs to be JSON formatted.
constexpr char kInitialIntentParameterData[] = "\"initial\"";
constexpr char kModuleName[] = "mod_name";
constexpr char kStoryName[] = "story";
constexpr char kIntentAction[] = "action";

class IntentsTest : public modular_testing::TestHarnessFixture {
 protected:
  IntentsTest()
      : number_of_intents_handled_(0),
        test_module_(
            modular_testing::FakeComponent::Args{
                .url = modular_testing::TestHarnessBuilder::GenerateFakeUrl(),
                .sandbox_services = modular_testing::FakeModule::GetDefaultSandboxServices()},
            [this](fuchsia::modular::Intent intent) {
              latest_handled_intent_ = std::move(intent);
              number_of_intents_handled_++;
            }) {
    builder_.InterceptComponent(test_module_.BuildInterceptOptions());
    builder_.BuildAndRun(test_harness());
  }

  // Create an Intent with the given handler, parameter name, and parameter
  // data.
  fuchsia::modular::Intent CreateIntent(std::string handler, std::string parameter_name,
                                        std::string parameter_data) {
    fuchsia::modular::Intent intent;
    intent.handler = handler;
    intent.action = kIntentAction;

    fuchsia::modular::IntentParameter intent_parameter;
    intent_parameter.name = parameter_name;
    intent_parameter.data = fuchsia::modular::IntentParameterData();
    fsl::SizedVmo vmo;
    FX_CHECK(fsl::VmoFromString(parameter_data, &vmo));
    intent_parameter.data.set_json(std::move(vmo).ToTransport());
    intent.parameters.emplace();
    intent.parameters->push_back(std::move(intent_parameter));

    return intent;
  }

  // Starts a second module by calling AddModuleToStory() using the
  // ModuleContext of the original module. The intent is expected to be handled
  // by the original module if the modules' intent handlers match.
  void AddModuleToStory(fuchsia::modular::ModuleContext* const module_context,
                        fuchsia::modular::Intent intent,
                        fidl::InterfaceRequest<fuchsia::modular::ModuleController> request,
                        bool* started) {
    module_context->AddModuleToStory(
        kModuleName, std::move(intent), std::move(request), nullptr,
        [started](const fuchsia::modular::StartModuleStatus) mutable { *started = true; });
  }

  // Checks that the given intent's paramter name and data matches expectations.
  bool IntentMatchesExpectations(fuchsia::modular::Intent* intent, std::string expected_param_name,
                                 std::string expected_param_data) {
    for (const auto& parameter : *intent->parameters) {
      if (parameter.data.is_json()) {
        if (parameter.name.value_or("") == expected_param_name) {
          std::string parameter_data;
          FX_CHECK(fsl::StringFromVmo(parameter.data.json(), &parameter_data));
          if (expected_param_data == parameter_data) {
            return true;
          }
        }
      }
    }
    return false;
  }

  int number_of_intents_handled_;
  modular_testing::FakeModule test_module_;
  modular_testing::TestHarnessBuilder builder_;
  fuchsia::modular::Intent latest_handled_intent_;
};

// Launches a single module with an intent. Checks that the module exposes an
// intent handler and gets notified of the intent by the framework.
TEST_F(IntentsTest, ModuleUsesIntentHandler) {
  // Launch initial module
  auto initial_module_intent =
      CreateIntent(test_module_.url(), kIntentParameterName, kInitialIntentParameterData);
  modular_testing::AddModToStory(test_harness(), kStoryName, kModuleName,
                                 std::move(initial_module_intent));
  RunLoopUntil([&] { return number_of_intents_handled_ == 1; });
  ASSERT_TRUE(test_module_.is_running());

  // Check that the intent handler received the intent
  EXPECT_TRUE(IntentMatchesExpectations(&latest_handled_intent_, kIntentParameterName,
                                        kInitialIntentParameterData));
}

// Launches a module that exposes an intent handler service then tests that a
// second intent sent to an already running module with the same parameters but
// different data notifies the intent handler of the new intent.
TEST_F(IntentsTest, ReuseIntentHandlerSameParamName) {
  // Launch initial module
  auto initial_module_intent =
      CreateIntent(test_module_.url(), kIntentParameterName, kInitialIntentParameterData);
  modular_testing::AddModToStory(test_harness(), kStoryName, kModuleName,
                                 std::move(initial_module_intent));
  RunLoopUntil([&] { return number_of_intents_handled_ == 1; });
  ASSERT_TRUE(test_module_.is_running());

  // Launch second module using first module's |module_context|
  fuchsia::modular::ModuleControllerPtr second_module_controller;
  bool module_started{false};

  // Use different param data
  auto second_module_param_data = "\"second_module_param_data\"";
  AddModuleToStory(test_module_.module_context(),
                   CreateIntent(test_module_.url(), kIntentParameterName, second_module_param_data),
                   second_module_controller.NewRequest(), &module_started);

  RunLoopUntil([&] { return number_of_intents_handled_ == 2; });

  EXPECT_TRUE(IntentMatchesExpectations(&latest_handled_intent_, kIntentParameterName,
                                        second_module_param_data));
}

// Launches a module that exposes an intent handler service then tests that a
// second intent with different parameters is delivered to to the already
// running intent handler.
TEST_F(IntentsTest, ReuseIntentHandlerDifferentParam) {
  // Launch initial module
  auto initial_module_intent =
      CreateIntent(test_module_.url(), kIntentParameterName, kInitialIntentParameterData);
  modular_testing::AddModToStory(test_harness(), kStoryName, kModuleName,
                                 std::move(initial_module_intent));
  RunLoopUntil([&] { return number_of_intents_handled_ == 1; });
  ASSERT_TRUE(test_module_.is_running());

  // Launch second module using first module's |module_context|
  fuchsia::modular::ModuleControllerPtr second_module_controller;
  bool module_started{false};

  // Use different param name and data
  auto second_module_param_name = "second_param_name";
  auto second_module_param_data = "\"second_module_param_data\"";
  AddModuleToStory(
      test_module_.module_context(),
      CreateIntent(test_module_.url(), second_module_param_name, second_module_param_data),
      second_module_controller.NewRequest(), &module_started);

  RunLoopUntil([&] { return number_of_intents_handled_ == 2; });

  EXPECT_TRUE(IntentMatchesExpectations(&latest_handled_intent_, second_module_param_name,
                                        second_module_param_data));
}

// Launches a module that exposes an intent handler service then tests that a
// second intent with different handler is not delivered to the running
// intent handler.
TEST_F(IntentsTest, DifferentHandler) {
  // Launch initial module
  auto initial_module_intent =
      CreateIntent(test_module_.url(), kIntentParameterName, kInitialIntentParameterData);
  modular_testing::AddModToStory(test_harness(), kStoryName, kModuleName,
                                 std::move(initial_module_intent));
  RunLoopUntil([&] { return number_of_intents_handled_ == 1; });
  ASSERT_TRUE(test_module_.is_running());

  // Launch second module using first module's |module_context|
  fuchsia::modular::ModuleControllerPtr second_module_controller;
  bool module_started{false};

  // Use different handler
  auto different_module_url = modular_testing::TestHarnessBuilder::GenerateFakeUrl();
  auto different_intent_param_name = "different_param_name";
  auto different_intent_param_data = "\"different_param_data\"";
  auto different_intent =
      CreateIntent(different_module_url, different_intent_param_name, different_intent_param_data);
  AddModuleToStory(test_module_.module_context(), std::move(different_intent),
                   second_module_controller.NewRequest(), &module_started);

  RunLoopUntil([&] { return module_started; });

  // Check that the intercepted_module_'s latest handled intent matches the
  // initial module
  EXPECT_TRUE(IntentMatchesExpectations(&latest_handled_intent_, kIntentParameterName,
                                        kInitialIntentParameterData));
}

}  // namespace
