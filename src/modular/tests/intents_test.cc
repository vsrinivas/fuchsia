// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/fsl/vmo/strings.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_module.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"

namespace {

constexpr char kModuleName[] = "mod_name";
constexpr char kStoryName[] = "story";
constexpr char kIntentAction[] = "action";

class IntentsTest : public modular_testing::TestHarnessFixture {
 protected:
  IntentsTest()
      : test_module_(modular_testing::FakeComponent::Args{
            .url = modular_testing::TestHarnessBuilder::GenerateFakeUrl(),
            .sandbox_services = modular_testing::FakeModule::GetDefaultSandboxServices()}) {
    builder_.InterceptComponent(test_module_.BuildInterceptOptions());
    builder_.BuildAndRun(test_harness());
  }

  // Create an Intent with the given handler.
  fuchsia::modular::Intent CreateIntent(std::string handler) {
    fuchsia::modular::Intent intent;
    intent.handler = handler;
    intent.action = kIntentAction;

    return intent;
  }

  modular_testing::FakeModule test_module_;
  modular_testing::TestHarnessBuilder builder_;
};

// Launches a single module with an intent. Checks that the module exposes an
// intent handler and gets notified of the intent by the framework.
TEST_F(IntentsTest, ModuleUsesIntentHandler) {
  // Launch initial module
  auto initial_module_intent = CreateIntent(test_module_.url());
  modular_testing::AddModToStory(test_harness(), kStoryName, kModuleName,
                                 std::move(initial_module_intent));
  RunLoopUntil([&] { return test_module_.is_running(); });
}

}  // namespace
