// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/app/discover/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/modular_test_harness/cpp/fake_component.h>
#include <lib/modular_test_harness/cpp/fake_module.h>
#include <lib/modular_test_harness/cpp/test_harness_fixture.h>
#include <sdk/lib/sys/cpp/testing/test_with_environment.h>

namespace {

constexpr char kModuleName[] = "mod_name";
constexpr char kStoryName[] = "story";
constexpr char kIntentAction[] = "action";
constexpr char kTestData[] = "test-data";
constexpr char kTestType[] = "test-type";

class ModuleOutputTest : public modular::testing::TestHarnessFixture {
 public:
  void SetUp() override {
    test_module_ = std::make_unique<modular::testing::FakeModule>(
        [this](fuchsia::modular::Intent intent) {});
    test_module_url_ = builder_.GenerateFakeUrl();
    builder_.InterceptComponent(
        test_module_->GetOnCreateHandler(),
        {.url = test_module_url_,
         .sandbox_services = {"fuchsia.app.discover.ModuleOutputWriter",
                              "fuchsia.modular.ModuleContext"}});

    test_harness().events().OnNewComponent =
        builder_.BuildOnNewComponentHandler();
    test_harness()->Run(builder_.BuildSpec());
  }

  std::unique_ptr<modular::testing::FakeModule> test_module_;
  modular::testing::TestHarnessBuilder builder_;
  std::string test_module_url_;
  std::string test_entity_provider_agent_url_;
};

TEST_F(ModuleOutputTest, ModuleWritesToOutput) {
  fuchsia::modular::Intent intent;
  intent.handler = test_module_url_;
  intent.action = kIntentAction;

  AddModToStory(std::move(intent), kModuleName, kStoryName);
  RunLoopUntil(
      [&] { return test_module_->is_running(); });

  fsl::SizedVmo vmo;
  fsl::VmoFromString(kTestData, &vmo);
  fuchsia::modular::EntityPtr entity;
  fidl::StringPtr reference;
  test_module_->module_context()->CreateEntity(
      kTestType, std::move(vmo).ToTransport(), entity.NewRequest(),
      [&reference](fidl::StringPtr entity_reference) {
        reference = std::move(entity_reference);
      });

  RunLoopUntil([&] { return !reference.is_null(); });

  fuchsia::app::discover::ModuleOutputWriterPtr module_output;
  test_module_->component_context()->svc()->Connect(module_output.NewRequest());
  bool output_written{false};
  module_output->Write("output_name", reference,
                       [&output_written](auto result) {
                         // TODO: once the discover service generates
                         // suggestions, we should ensure they are generated
                         // based on this modules output.
                         ASSERT_TRUE(result.is_response());
                         output_written = true;
                       });

  RunLoopUntil([&output_written] { return output_written; });
}

}  // namespace
