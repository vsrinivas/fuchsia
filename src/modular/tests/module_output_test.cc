// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/app/discover/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/modular_test_harness/cpp/fake_component.h>
#include <lib/modular_test_harness/cpp/test_harness_fixture.h>
#include <sdk/lib/sys/cpp/testing/test_with_environment.h>

namespace {

constexpr char kModuleName[] = "mod_name";
constexpr char kStoryName[] = "story";
constexpr char kIntentAction[] = "action";

constexpr zx::duration kTimeout = zx::sec(15);

class ModuleOutputTest : public modular::testing::TestHarnessFixture {
 public:
  void SetUp() override {
    test_module_url_ = builder_.GenerateFakeUrl();
    builder_.InterceptComponent(
        component_.GetOnCreateHandler(),
        {.url = test_module_url_,
         .sandbox_services = {"fuchsia.app.discover.ModuleOutputWriter",
                              "fuchsia.modular.ModuleContext"}});

    test_harness().events().OnNewComponent =
        builder_.BuildOnNewComponentHandler();
    test_harness()->Run(builder_.BuildSpec());
  }

 protected:
  void CreateStoryAndAddMod(fuchsia::modular::Intent intent) {
    // Create an AddMod command
    fuchsia::modular::AddMod add_mod;
    add_mod.mod_name = {kModuleName};
    add_mod.intent = std::move(intent);
    add_mod.surface_relation = fuchsia::modular::SurfaceRelation{};

    fuchsia::modular::StoryCommand cmd;
    cmd.set_add_mod(std::move(add_mod));

    std::vector<fuchsia::modular::StoryCommand> cmds;
    cmds.push_back(std::move(cmd));

    // Connect to PuppetMaster Service
    fuchsia::modular::PuppetMasterPtr puppet_master;
    fuchsia::modular::testing::ModularService svc;
    svc.set_puppet_master(puppet_master.NewRequest());
    test_harness()->ConnectToModularService(std::move(svc));

    // Create a story
    fuchsia::modular::StoryPuppetMasterPtr story_master;
    puppet_master->ControlStory(kStoryName, story_master.NewRequest());

    // Add the initial module to the story
    story_master->Enqueue(std::move(cmds));
    story_master->Execute([&](fuchsia::modular::ExecuteResult result) {});
  }

  modular::testing::FakeComponent component_;
  modular::testing::TestHarnessBuilder builder_;
  std::string test_module_url_;
};

TEST_F(ModuleOutputTest, ModuleWritesToOutput) {
  fuchsia::modular::Intent intent;
  intent.handler = test_module_url_;
  intent.action = kIntentAction;

  CreateStoryAndAddMod(std::move(intent));
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&] { return component_.is_running(); },
                                        kTimeout));

  fuchsia::app::discover::ModuleOutputWriterPtr module_output;
  component_.component_context()->svc()->Connect(module_output.NewRequest());
  bool output_written{false};
  module_output->Write("output_name", "reference",
                       [&output_written](auto result) {
                         // TODO: once the discover service generates
                         // suggestions, we should ensure they are generated
                         // based on this modules output.
                         ASSERT_TRUE(result.is_response());
                         output_written = true;
                       });

  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&output_written] { return output_written; }, kTimeout));
}

}  // namespace
