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
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"
#include "peridot/tests/chain/defs.h"
#include "peridot/tests/common/defs.h"

using modular::testing::Await;
using modular::testing::Put;
using modular::testing::TestPoint;

namespace {

// Cf. README.md for what this test does and how.
class TestApp
    : public modular::testing::ComponentBase<fuchsia::modular::UserShell> {
 public:
  TestApp(component::StartupContext* const startup_context)
      : ComponentBase(startup_context) {
    TestInit(__FILE__);
  }

  ~TestApp() override = default;

 private:
  TestPoint initialize_{"Initialize()"};

  // |fuchsia::modular::UserShell|
  void Initialize(fidl::InterfaceHandle<fuchsia::modular::UserShellContext>
                      user_shell_context) override {
    initialize_.Pass();
    user_shell_context_.Bind(std::move(user_shell_context));
    user_shell_context_->GetStoryProvider(story_provider_.NewRequest());

    CreateStory();
  }

  TestPoint create_story_{"CreateStory()"};

  void CreateStory() {
    // Create an empty Story. Once it has been created, add our first Module.
    story_provider_->CreateStory(
        nullptr /* module_url */, [this](const fidl::StringPtr& story_id) {
          create_story_.Pass();
          story_id_ = story_id;
          story_provider_->GetController(story_id_,
                                         story_controller_.NewRequest());
          AddRootModule();
        });
  }

  void AddRootModule() {
    fuchsia::modular::Intent intent;
    intent.handler = kModuleUrl;

    fuchsia::modular::IntentParameterData data;
    fsl::SizedVmo vmo;
    FXL_CHECK(fsl::VmoFromString(R"("initial data for the story")", &vmo));
    data.set_json(std::move(vmo).ToTransport());
    fuchsia::modular::IntentParameter intent_parameter;
    intent_parameter.name = "rootModuleNoun1";
    intent_parameter.data = std::move(data);
    intent.parameters.push_back(std::move(intent_parameter));
    story_controller_->AddModule({}, "rootMod", std::move(intent),
                                 nullptr /* surface_relation */);
    fidl::VectorPtr<fidl::StringPtr> path;
    path.reset({"rootMod"});
    story_controller_->GetModuleController(std::move(path),
                                           child_module_.NewRequest());
    StartStory();
  }

  void StartStory() {
    // Start and show the new story.
    fidl::InterfacePtr<fuchsia::ui::viewsv1token::ViewOwner> story_view_binding;
    story_controller_->Start(story_view_binding.NewRequest());
  }

  fuchsia::modular::UserShellContextPtr user_shell_context_;
  fuchsia::modular::StoryProviderPtr story_provider_;
  fidl::StringPtr story_id_;
  fuchsia::modular::StoryControllerPtr story_controller_;
  fuchsia::modular::ModuleControllerPtr child_module_;
  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  modular::testing::ComponentMain<TestApp>();
  return 0;
}
