// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/connect.h"
#include "lib/app_driver/cpp/app_driver.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/story/fidl/link.fidl.h"
#include "lib/ui/views/fidl/view_manager.fidl.h"
#include "lib/user/fidl/user_shell.fidl.h"
#include "peridot/examples/counter_cpp/store.h"
#include "peridot/lib/fidl/single_service_app.h"
#include "peridot/lib/rapidjson/rapidjson.h"
#include "peridot/lib/testing/component_base.h"
#include "peridot/lib/testing/reporting.h"
#include "peridot/lib/testing/testing.h"

namespace {

constexpr char kUserShell[] =
    "https://fuchsia.googlesource.com/modular/"
    "services/user/user_shell.fidl#modular.UserShell";

constexpr char kTestApp[] =
    "https://fuchsia.googlesource.com/modular/"
    "tests/link_data/test_link_data.cc#TestApp";

bool IsRunning(const modular::StoryState state) {
  switch (state) {
    case modular::StoryState::STARTING:
    case modular::StoryState::RUNNING:
    case modular::StoryState::DONE:
      return true;
    case modular::StoryState::INITIAL:
    case modular::StoryState::STOPPED:
    case modular::StoryState::ERROR:
      return false;
  }
}

class Settings {
 public:
  explicit Settings(const fxl::CommandLine& command_line) {
    first_module = command_line.GetOptionValueWithDefault("first_module",
                                                          "example_recipe");
  }

  std::string first_module;
};

// A simple link watcher implementation that after every 5 updates of a Link
// invokes a "continue" callback. Used to push the test sequence forward after a
// module in the test story was running for a bit.
class LinkChangeCountWatcherImpl : modular::LinkWatcher {
 public:
  LinkChangeCountWatcherImpl() : binding_(this) {}
  ~LinkChangeCountWatcherImpl() override = default;

  // Registers itself as watcher on the given link. Only one link at a time can
  // be watched.
  void Watch(modular::LinkPtr* const link) {
    (*link)->Watch(binding_.NewBinding());
  }

  // Deregisters itself from the watched link.
  void Reset() { binding_.Close(); }

  // Sets the function where to continue after enough changes were observed on
  // the link.
  void Continue(std::function<void()> at) { continue_ = at; }

 private:
  // |LinkWatcher|
  void Notify(const fidl::String& json) override {
    modular_example::Counter counter =
        modular_example::Store::ParseCounterJson(json.get(), "test_link_data");

    if (counter.is_valid() && counter.counter > last_continue_count_) {
      if (counter.counter % 5 == 0) {
        last_continue_count_ = counter.counter;
        continue_();
      }
    }
  }

  int last_continue_count_{};
  std::function<void()> continue_;
  fidl::Binding<modular::LinkWatcher> binding_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LinkChangeCountWatcherImpl);
};

// A simple story watcher implementation that invokes a "continue" callback when
// it sees the watched story transition to DONE state. Used to push the test
// sequence forward when the test story is done.
class StoryStateWatcherImpl : modular::StoryWatcher {
 public:
  StoryStateWatcherImpl() : binding_(this) {}
  ~StoryStateWatcherImpl() override = default;

  // Registers itself as a watcher on the given story. Only one story at a time
  // can be watched.
  void Watch(modular::StoryControllerPtr* const story_controller) {
    (*story_controller)->Watch(binding_.NewBinding());
  }

  // Deregisters itself from the watched story.
  void Reset() { binding_.Close(); }

  // Sets the function where to continue when the story is observed to be at
  // a particular state.
  void Continue(modular::StoryState state, std::function<void()> at) {
    auto state_index = static_cast<unsigned int>(state);
    if (continue_.size() <= state_index) {
      continue_.resize(state_index + 1);
    }
    continue_[state_index] = at;
  }

 private:
  // |StoryWatcher|
  void OnStateChange(modular::StoryState state) override {
    auto state_index = static_cast<unsigned int>(state);
    // TODO(jimbe) Need to investigate why we are getting two notifications for
    // each state transition.
    FXL_LOG(INFO) << "OnStateChange: " << state_index;
    if (continue_.size() > state_index && continue_[state_index]) {
      continue_[state_index]();
    }
  }

  // |StoryWatcher|
  void OnModuleAdded(modular::ModuleDataPtr module_data) override {
    FXL_LOG(INFO) << "OnModuleAdded: " << module_data->module_url;
    if (!on_module_added_called_) {
      on_module_added_.Pass();
      on_module_added_called_ = true;
    }
  }

  fidl::Binding<modular::StoryWatcher> binding_;
  std::vector<std::function<void()>> continue_;
  modular::testing::TestPoint on_module_added_{"OnModuleAdded"};
  bool on_module_added_called_{};

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryStateWatcherImpl);
};

// Tests the machinery that allows modules to coordinate through shared link
// data, and that these link data are persisted across story stop and
// resume. This is invoked as a user shell from device runner and executes a
// predefined sequence of steps, rather than to expose a UI to be driven by user
// interaction, as a user shell normally would.
//
// TODO(mesch): The example modules that this test uses should be converted to
// actual test modules that make assertions on their own, and copied or moved
// here.
class TestApp : public modular::testing::ComponentBase<modular::UserShell> {
 public:
  explicit TestApp(app::ApplicationContext* const application_context, Settings settings)
      : ComponentBase(application_context),
        settings_(std::move(settings)) {
    TestInit(__FILE__);
  }

  ~TestApp() override = default;

 private:
  using TestPoint = modular::testing::TestPoint;

  TestPoint initialize_{"Initialize()"};

  // |UserShell|
  void Initialize(fidl::InterfaceHandle<modular::UserShellContext>
                      user_shell_context) override {
    initialize_.Pass();

    user_shell_context_.Bind(std::move(user_shell_context));
    user_shell_context_->GetStoryProvider(story_provider_.NewRequest());

    TestStory1();
  }

  TestPoint story1_create_{"Story1 Create"};

  void TestStory1() {
    const std::string& url = settings_.first_module;

    modular::JsonDoc doc;
    std::vector<std::string> segments{"example", url, "created-with-info"};
    modular::CreatePointer(doc, segments.begin(), segments.end())
        .Set(doc, true);

    using FidlStringMap = fidl::Map<fidl::String, fidl::String>;
    story_provider_->CreateStoryWithInfo(url, FidlStringMap(),
                                         modular::JsonValueToString(doc),
                                         [this](const fidl::String& story_id) {
                                           story1_create_.Pass();
                                           TestStory1_GetController(story_id);
                                         });
  }

  TestPoint story1_get_controller_{"Story1 GetController"};

  void TestStory1_GetController(const fidl::String& story_id) {
    story_provider_->GetController(story_id, story_controller_.NewRequest());
    story_controller_->GetInfo(
        [this](modular::StoryInfoPtr story_info, modular::StoryState state) {
          story1_get_controller_.Pass();
          story_info_ = std::move(story_info);
          TestStory1_SetRootLink();
        });
  }

  // Totally tentative use of the root module link: Tell the root module under
  // what user shell it's running.
  void TestStory1_SetRootLink() {
    story_controller_->GetLink(nullptr, "root", root_link_.NewRequest());

    std::vector<std::string> segments{kUserShell};
    root_link_->Set(fidl::Array<fidl::String>::From(segments),
                    modular::JsonValueToString(modular::JsonValue(kTestApp)));

    TestStory1_Run(0);
  }

  TestPoint story1_run_{"Story1 Run"};

  void TestStory1_Run(const int round) {
    if (!story_controller_) {
      story_provider_->GetController(story_info_->id,
                                     story_controller_.NewRequest());
      story_controller_->GetLink(nullptr, "root", root_link_.NewRequest());
    }

    link_change_count_watcher_.Continue(
        [this, round] { TestStory1_Cycle(round); });
    link_change_count_watcher_.Watch(&root_link_);

    story_state_watcher_.Continue(modular::StoryState::DONE, [this] {
      story_controller_->Stop([this] {
        TeardownStoryController();
        story1_run_.Pass();

        // When the story is done, the test is over.
        fsl::MessageLoop::GetCurrent()->task_runner()->PostTask(
            [this] { user_shell_context_->Logout(); });
      });
    });
    story_state_watcher_.Watch(&story_controller_);

    // Start and show the new story.
    fidl::InterfaceHandle<mozart::ViewOwner> story_view;
    story_controller_->Start(story_view.NewRequest());
  }

  TestPoint story1_cycle1_{"Story1 Cycle 1"};
  TestPoint story1_cycle2_{"Story1 Cycle 2"};

  // Every five counter increments, we dehydrate and rehydrate the story, until
  // the story stops itself when it reaches 11 counter increments.
  void TestStory1_Cycle(const int round) {
    if (round == 0) {
      story1_cycle1_.Pass();
    } else if (round == 1) {
      story1_cycle2_.Pass();
      // We don't cycle anymore and wait for DONE state to be reached.
      return;
    }

    // When the story stops, we start it again.
    story_state_watcher_.Continue(modular::StoryState::STOPPED, [this, round] {
      story_state_watcher_.Continue(modular::StoryState::STOPPED, nullptr);
      story_provider_->GetStoryInfo(
        story_info_->id, [this, round](modular::StoryInfoPtr story_info) {
          FXL_CHECK(story_info);

          // Can't use the StoryController here because we closed it
          // in TeardownStoryController().
          story_provider_->RunningStories(
              [this, round](fidl::Array<fidl::String> story_ids) {
                auto n = count(story_ids.begin(), story_ids.end(),
                               story_info_->id);
                FXL_CHECK(n == 0);
                TestStory1_Run(round + 1);
              });
        });
    });

    story_controller_->GetInfo([this, round](modular::StoryInfoPtr story_info,
                                             modular::StoryState state) {
      FXL_CHECK(!story_info.is_null());
      FXL_CHECK(IsRunning(state));

      story_controller_->Stop([this, round] { TeardownStoryController(); });
    });
  }

  void TeardownStoryController() {
    story_state_watcher_.Reset();
    link_change_count_watcher_.Reset();
    story_controller_.reset();
    root_link_.reset();
  }

  const Settings settings_;

  StoryStateWatcherImpl story_state_watcher_;
  LinkChangeCountWatcherImpl link_change_count_watcher_;

  modular::UserShellContextPtr user_shell_context_;
  modular::StoryProviderPtr story_provider_;
  modular::StoryControllerPtr story_controller_;
  modular::LinkPtr root_link_;
  modular::StoryInfoPtr story_info_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestApp);
};

}  // namespace

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  Settings settings(command_line);

  fsl::MessageLoop loop;

  auto app_context = app::ApplicationContext::CreateFromStartupInfo();
  modular::AppDriver<TestApp> driver(
      app_context->outgoing_services(),
      std::make_unique<TestApp>(app_context.get(), std::move(settings)),
      [&loop] { loop.QuitNow(); });

  loop.Run();
  return 0;
}
