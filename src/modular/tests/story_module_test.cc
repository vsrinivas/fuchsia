// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/testing/cpp/fidl.h>

#include "src/lib/fsl/vmo/strings.h"
#include "src/modular/lib/modular_test_harness/cpp/fake_module.h"
#include "src/modular/lib/modular_test_harness/cpp/test_harness_fixture.h"

namespace {

constexpr char kModuleName[] = "mod_name";
constexpr char kStoryName[] = "story";
constexpr char kIntentAction[] = "action";
constexpr char kTestData[] = "test-data";
constexpr char kTestType[] = "test-type";

class StoryModuleTest : public modular_testing::TestHarnessFixture {
 public:
  StoryModuleTest() {
    test_module_ = std::make_unique<modular_testing::FakeModule>(
        modular_testing::FakeComponent::Args{
            .url = modular_testing::TestHarnessBuilder::GenerateFakeUrl(),
            .sandbox_services = {"fuchsia.modular.ModuleContext"}},
        [](fuchsia::modular::Intent intent) {});
    builder_.InterceptComponent(test_module_->BuildInterceptOptions());
    builder_.BuildAndRun(test_harness());
  }

  std::unique_ptr<modular_testing::FakeModule> test_module_;
  modular_testing::TestHarnessBuilder builder_;
  std::string test_entity_provider_agent_url_;
};

TEST_F(StoryModuleTest, ModuleWritesToOutput) {
  fuchsia::modular::Intent intent;
  intent.handler = test_module_->url();
  intent.action = kIntentAction;

  modular_testing::AddModToStory(test_harness(), kStoryName, kModuleName, std::move(intent));
  RunLoopUntil([&] { return test_module_->is_running(); });

  fsl::SizedVmo vmo;
  fsl::VmoFromString(kTestData, &vmo);
  fuchsia::modular::EntityPtr entity;
  fidl::StringPtr reference;
  test_module_->module_context()->CreateEntity(
      kTestType, std::move(vmo).ToTransport(), entity.NewRequest(),
      [&reference](fidl::StringPtr entity_reference) { reference = std::move(entity_reference); });

  RunLoopUntil([&] { return reference.has_value(); });
}

}  // namespace
