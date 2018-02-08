// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <sstream>
#include <string>

#include "lib/app/cpp/application_context.h"
#include "lib/context/cpp/formatting.h"
#include "lib/context/fidl/context_reader.fidl.h"
#include "lib/context/fidl/context_writer.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/ui/views/fidl/view_manager.fidl.h"
#include "lib/ui/views/fidl/view_provider.fidl.h"
#include "lib/user/fidl/focus.fidl.h"
#include "lib/user/fidl/user_shell.fidl.h"
#include "peridot/lib/fidl/array_to_string.h"
#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/testing/component_base.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"

namespace modular {
namespace {

constexpr char kModuleUrl[] =
    "file:///system/test/modular_tests/chain_test_module";

// Tests starting Modules with a Daisy and the subsequent initialization of the
// Module's Links based on the values of Daisy.nouns.
class TestApp : public testing::ComponentBase<UserShell>, StoryWatcher {
 public:
  TestApp(app::ApplicationContext* const application_context)
      : ComponentBase(application_context), story_watcher_binding_(this) {
    TestInit(__FILE__);
  }

  ~TestApp() override = default;

 private:
  using TestPoint = testing::TestPoint;

  TestPoint initialize_{"Initialize()"};

  // |UserShell|
  void Initialize(
      fidl::InterfaceHandle<UserShellContext> user_shell_context) override {
    initialize_.Pass();

    user_shell_context_.Bind(std::move(user_shell_context));

    user_shell_context_->GetStoryProvider(story_provider_.NewRequest());

    CreateStory();
  }

  // |StoryWatcher|
  void OnStateChange(StoryState state) override {
    if (state == StoryState::DONE)
      Logout();
  }

  // |StoryWatcher|
  void OnModuleAdded(ModuleDataPtr module_data) override {}

  TestPoint create_story_{"CreateStory()"};

  void CreateStory() {
    // TODO(thatguy): CreateStory() should take a Daisy as well, or not add any
    // Module in the first place.
    story_provider_->CreateStory(kModuleUrl,
                                 [this](const fidl::String& story_id) {
                                   story_id_ = story_id;
                                   create_story_.Pass();
                                   StartStory();
                                 });
  }

  void StartStory() {
    story_provider_->GetController(story_id_, story_controller_.NewRequest());

    // Start and show the new story.
    fidl::InterfaceHandle<mozart::ViewOwner> story_view;
    story_controller_->Start(story_view.NewRequest());

    StoryWatcherPtr watcher;
    story_watcher_binding_.Bind(watcher.NewRequest());
    story_controller_->Watch(std::move(watcher));
  }

  void Logout() { user_shell_context_->Logout(); }

  UserShellContextPtr user_shell_context_;
  StoryProviderPtr story_provider_;

  fidl::String story_id_;
  StoryControllerPtr story_controller_;

  fidl::Binding<StoryWatcher> story_watcher_binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace
}  // namespace modular

int main(int /*argc*/, const char** /*argv*/) {
  modular::testing::ComponentMain<modular::TestApp>();
  return 0;
}
