// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <sstream>
#include <string>

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/viewsv1token/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/context/cpp/formatting.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>

#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/testing/component_base.h"
#include "peridot/public/lib/integration_testing/cpp/reporting.h"
#include "peridot/public/lib/integration_testing/cpp/testing.h"
#include "peridot/tests/chain/defs.h"
#include "peridot/tests/common/defs.h"

using modular::testing::Await;
using modular::testing::Put;
using modular::testing::TestPoint;

namespace {

const char kStoryName[] = "story";

// Cf. README.md for what this test does and how.
class TestApp : public modular::testing::ComponentBase<void> {
 public:
  TestApp(component::StartupContext* const startup_context)
      : ComponentBase(startup_context) {
    TestInit(__FILE__);

    session_shell_context_ = startup_context->ConnectToEnvironmentService<
        fuchsia::modular::SessionShellContext>();
    session_shell_context_->GetStoryProvider(story_provider_.NewRequest());

    puppet_master_ =
        startup_context
            ->ConnectToEnvironmentService<fuchsia::modular::PuppetMaster>();

    CreateStory();
  }

  ~TestApp() override = default;

 private:
  TestPoint create_story_{"CreateStory()"};

  void CreateStory() {
    puppet_master_->ControlStory("story", story_puppet_master_.NewRequest());

    fidl::VectorPtr<fuchsia::modular::StoryCommand> commands;
    fuchsia::modular::AddMod add_mod;
    add_mod.mod_name.push_back("root");
    add_mod.intent.action = "action";
    add_mod.intent.handler = kModuleUrl;

    fuchsia::modular::IntentParameterData data;
    fsl::SizedVmo vmo;
    FXL_CHECK(fsl::VmoFromString(R"("initial data for the story")", &vmo));
    data.set_json(std::move(vmo).ToTransport());
    fuchsia::modular::IntentParameter intent_parameter;
    intent_parameter.name = "rootModuleParam1";
    intent_parameter.data = std::move(data);
    add_mod.intent.parameters.push_back(std::move(intent_parameter));
    add_mod.surface_parent_mod_name.resize(0);

    fuchsia::modular::StoryCommand command;
    command.set_add_mod(std::move(add_mod));
    commands.push_back(std::move(command));

    story_puppet_master_->Enqueue(std::move(commands));
    story_puppet_master_->Execute(
        [this](fuchsia::modular::ExecuteResult result) {
          create_story_.Pass();
          StartStory();
        });
  }

  void StartStory() {
    // Start and show the new story.
    story_provider_->GetController(kStoryName, story_controller_.NewRequest());
    fidl::InterfacePtr<fuchsia::ui::viewsv1token::ViewOwner> story_view_binding;
    story_controller_->Start(story_view_binding.NewRequest());
  }

  fuchsia::modular::SessionShellContextPtr session_shell_context_;
  fuchsia::modular::PuppetMasterPtr puppet_master_;
  fuchsia::modular::StoryPuppetMasterPtr story_puppet_master_;
  fuchsia::modular::StoryProviderPtr story_provider_;
  fidl::StringPtr story_id_;
  fuchsia::modular::StoryControllerPtr story_controller_;
  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  modular::testing::ComponentMain<TestApp>();
  return 0;
}
