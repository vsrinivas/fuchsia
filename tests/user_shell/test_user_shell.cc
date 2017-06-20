// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "application/lib/app/connect.h"
#include "application/services/service_provider.fidl.h"
#include "apps/maxwell/services/suggestion/suggestion_provider.fidl.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/fidl/single_service_view_app.h"
#include "apps/modular/lib/fidl/view_host.h"
#include "apps/modular/lib/rapidjson/rapidjson.h"
#include "apps/modular/lib/testing/reporting.h"
#include "apps/modular/lib/testing/testing.h"
#include "apps/modular/services/story/link.fidl.h"
#include "apps/modular/services/user/user_context.fidl.h"
#include "apps/modular/services/user/user_shell.fidl.h"
#include "apps/mozart/lib/view_framework/base_view.h"
#include "apps/mozart/services/views/view_manager.fidl.h"
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "apps/test_runner/services/test_runner.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/tasks/task_runner.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

constexpr char kUserShell[] =
    "https://fuchsia.googlesource.com/modular/"
    "services/user/user_shell.fidl#modular.UserShell";

constexpr char kTestUserShellApp[] =
    "https://fuchsia.googlesource.com/modular/"
    "tests/user_shell/test_user_shell.cc#TestUserShellApp";

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
  explicit Settings(const ftl::CommandLine& command_line) {
    // Good other value to use for dev:
    // "file:///system/apps/example_flutter_counter_parent"
    first_module = command_line.GetOptionValueWithDefault(
        "first_module", "file:///system/apps/example_recipe");
    second_module = command_line.GetOptionValueWithDefault(
        "second_module", "file:///system/apps/example_flutter_hello_world");
  }

  std::string first_module;
  std::string second_module;
};

// A simple link watcher implementation that after every 5 updates of a Link
// invokes a "continue" callback. Used to push the test sequence forward after a
// module in the test story was running for a bit.
class LinkWatcherImpl : modular::LinkWatcher {
 public:
  LinkWatcherImpl() : binding_(this) {}
  ~LinkWatcherImpl() override = default;

  // Registers itself as watcher on the given link. Only one link at a time can
  // be watched.
  void Watch(modular::LinkPtr* const link) {
    (*link)->Watch(binding_.NewBinding());
    data_count_ = 0;
  }

  // Deregisters itself from the watched link.
  void Reset() { binding_.Close(); }

  // Sets the function where to continue after enough changes were observed on
  // the link.
  void Continue(std::function<void()> at) { continue_ = at; }

 private:
  // |LinkWatcher|
  void Notify(const fidl::String& json) override {
    if (++data_count_ % 5 == 0) {
      continue_();
    }
  }

  int data_count_{0};
  std::function<void()> continue_;
  fidl::Binding<modular::LinkWatcher> binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LinkWatcherImpl);
};

// A simple story watcher implementation that invokes a "continue" callback when
// it sees the watched story transition to DONE state. Used to push the test
// sequence forward when the test story is done.
class StoryWatcherImpl : modular::StoryWatcher {
 public:
  StoryWatcherImpl() : binding_(this) {}
  ~StoryWatcherImpl() override = default;

  // Registers itself as a watcher on the given story. Only one story at a time
  // can be watched.
  void Watch(modular::StoryControllerPtr* const story_controller) {
    (*story_controller)->Watch(binding_.NewBinding());
  }

  // Deregisters itself from the watched story.
  void Reset() { binding_.Close(); }

  // Sets the function where to continue when the story is observed to be done.
  void Continue(std::function<void()> at) { continue_ = at; }

 private:
  // |StoryWatcher|
  void OnStateChange(modular::StoryState state) override {
    if (state != modular::StoryState::DONE) {
      return;
    }

    continue_();
  }

  // |StoryWatcher|
  void OnModuleAdded(modular::ModuleDataPtr module_data) override {
    FTL_LOG(INFO) << "OnModuleAdded: " << module_data->module_url;
    if (!on_module_added_called_) {
      on_module_added_.Pass();
      on_module_added_called_ = true;
    }
  }

  fidl::Binding<modular::StoryWatcher> binding_;
  std::function<void()> continue_;
  modular::testing::TestPoint on_module_added_{"OnModuleAdded"};
  bool on_module_added_called_ = false;
  FTL_DISALLOW_COPY_AND_ASSIGN(StoryWatcherImpl);
};

// A simple story modules watcher implementation that just logs the
// notifications it receives.
class StoryModulesWatcherImpl : modular::StoryModulesWatcher {
 public:
  StoryModulesWatcherImpl() : binding_(this) {}
  ~StoryModulesWatcherImpl() override = default;

  // Registers itself as a watcher on the given story. Only one story at a time
  // can be watched.
  void Watch(modular::StoryControllerPtr* const story_controller) {
    (*story_controller)->GetActiveModules(
        binding_.NewBinding(),
        [this](fidl::Array<modular::ModuleDataPtr> data) {
          FTL_LOG(INFO) << "StoryModulesWatcherImpl GetModules(): "
                        << data.size() << " modules";
        });
  }

  // Deregisters itself from the watched story.
  void Reset() { binding_.Close(); }

 private:
  // |StoryModulesWatcher|
  void OnNewModule(modular::ModuleDataPtr data) override {
    FTL_LOG(INFO) << "New Module: " << data->module_url;
  }

  // |StoryModulesWatcher|
  void OnStopModule(modular::ModuleDataPtr data) override {
    FTL_LOG(INFO) << "Stop Module: " << data->module_url;
  }

  fidl::Binding<modular::StoryModulesWatcher> binding_;
  FTL_DISALLOW_COPY_AND_ASSIGN(StoryModulesWatcherImpl);
};

// A simple story links watcher implementation that just logs the notifications
// it receives.
class StoryLinksWatcherImpl : modular::StoryLinksWatcher {
 public:
  StoryLinksWatcherImpl() : binding_(this) {}
  ~StoryLinksWatcherImpl() override = default;

  // Registers itself as a watcher on the given story. Only one story at a time
  // can be watched.
  void Watch(modular::StoryControllerPtr* const story_controller) {
    (*story_controller)->GetActiveLinks(
        binding_.NewBinding(),
        [this](fidl::Array<modular::LinkPathPtr> data) {
          FTL_LOG(INFO) << "StoryLinksWatcherImpl GetLinks(): "
                        << data.size() << " links";
        });
  }

  // Deregisters itself from the watched story.
  void Reset() { binding_.Close(); }

 private:
  // |StoryLinksWatcher|
  void OnNewLink(modular::LinkPathPtr data) override {
    FTL_LOG(INFO) << "New Link: " << data->link_name;
  }

  fidl::Binding<modular::StoryLinksWatcher> binding_;
  FTL_DISALLOW_COPY_AND_ASSIGN(StoryLinksWatcherImpl);
};

// A simple story provider watcher implementation. Just logs observed state
// transitions.
class StoryProviderWatcherImpl : modular::StoryProviderWatcher {
 public:
  StoryProviderWatcherImpl() : binding_(this) {}
  ~StoryProviderWatcherImpl() override = default;

  // Registers itself a watcher on the given story provider. Only one story
  // provider can be watched at a time.
  void Watch(modular::StoryProviderPtr* const story_provider) {
    (*story_provider)->Watch(binding_.NewBinding());
  }

  // Deregisters itself from the watched story provider.
  void Reset() { binding_.Close(); }

 private:
  // |StoryProviderWatcher|
  void OnDelete(const ::fidl::String& story_id) override {
    FTL_VLOG(1) << "TestUserShellApp::OnDelete() " << story_id;
  }

  // |StoryProviderWatcher|
  void OnChange(modular::StoryInfoPtr story_info,
                modular::StoryState story_state) override {
    FTL_VLOG(1) << "TestUserShellApp::OnChange() "
                << " id " << story_info->id << " url " << story_info->url;
  }

  fidl::Binding<modular::StoryProviderWatcher> binding_;
  FTL_DISALLOW_COPY_AND_ASSIGN(StoryProviderWatcherImpl);
};

// Tests the machinery available to a user shell implementation. This is invoked
// as a user shell from device runner and executes a predefined sequence of
// steps, rather than to expose a UI to be driven by user interaction, as a user
// shell normally would.
class TestUserShellApp : modular::SingleServiceViewApp<modular::UserShell> {
 public:
  // The app instance must be dynamic, because it needs to do several things
  // after its own constructor is invoked. It accomplishes that by being able to
  // call delete this. Cf. Terminate().
  static void New(const Settings& settings) {
    new TestUserShellApp(settings);  // will delete itself in Terminate().
  }

 private:
  TestUserShellApp(const Settings& settings) : settings_(settings) {
    modular::testing::Init(application_context(), __FILE__);
  }

  ~TestUserShellApp() override = default;

  using TestPoint = modular::testing::TestPoint;

  TestPoint create_view_{"CreateView()"};

  // |SingleServiceViewApp|
  void CreateView(
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      fidl::InterfaceRequest<app::ServiceProvider> services) override {
    create_view_.Pass();
    view_.reset(new modular::ViewHost(
        application_context()
            ->ConnectToEnvironmentService<mozart::ViewManager>(),
        std::move(view_owner_request)));
  }

  TestPoint initialize_{"Initialize()"};

  // |UserShell|
  void Initialize(fidl::InterfaceHandle<modular::UserContext> user_context,
                  fidl::InterfaceHandle<modular::UserShellContext>
                      user_shell_context) override {
    initialize_.Pass();

    user_context_.Bind(std::move(user_context));
    user_shell_context_.Bind(std::move(user_shell_context));
    user_shell_context_->GetStoryProvider(story_provider_.NewRequest());
    story_provider_watcher_.Watch(&story_provider_);

    TestStoryProvider_GetStoryInfo_Null();
  }

  TestPoint get_story_info_null_{"StoryProvider.GetStoryInfo() is null"};

  void TestStoryProvider_GetStoryInfo_Null() {
    story_provider_->GetStoryInfo("X",
                                  [this](modular::StoryInfoPtr story_info) {
                                    if (story_info.is_null()) {
                                      get_story_info_null_.Pass();
                                    }

                                    TestUserShellContext_GetLink();
                                  });
  }

  TestPoint get_link_{"UserShellContext.GetLink()"};

  void TestUserShellContext_GetLink() {
    user_shell_context_->GetLink(user_shell_link_.NewRequest());
    user_shell_link_->Get(nullptr, [this](const fidl::String& value) {
      get_link_.Pass();
      TestStoryProvider_PreviousStories();
    });
  }

  TestPoint previous_stories_{"StoryProvider.PreviousStories()"};

  void TestStoryProvider_PreviousStories() {
    story_provider_->PreviousStories([this](fidl::Array<fidl::String> stories) {
      previous_stories_.Pass();
      TestStoryProvider_GetStoryInfo(std::move(stories));
    });
  }

  TestPoint get_story_info_{"StoryProvider.GetStoryInfo()"};

  void TestStoryProvider_GetStoryInfo(fidl::Array<fidl::String> stories) {
    if (stories.size() == 0) {
      get_story_info_.Pass();
      TestStory1();
      return;
    }

    std::shared_ptr<unsigned int> count = std::make_shared<unsigned int>(0);
    for (auto& story_id : stories) {
      story_provider_->GetStoryInfo(
          story_id, [ this, story_id, count,
                      max = stories.size() ](modular::StoryInfoPtr story_info) {
            ++*count;

            if (!story_info.is_null()) {
              FTL_LOG(INFO) << "Previous story " << *count << " of " << max
                            << " " << story_id << " " << story_info->url;
            } else {
              FTL_LOG(INFO) << "Previous story " << *count << " of " << max
                            << " " << story_id << " was deleted";
            }

            if (*count == max) {
              get_story_info_.Pass();
              TestStory1();
            }
          });
    }
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
    story_controller_->GetLink(fidl::Array<fidl::String>::New(0), "root",
                               root_.NewRequest());

    std::vector<std::string> segments{kUserShell};
    root_->Set(
        fidl::Array<fidl::String>::From(segments),
        modular::JsonValueToString(modular::JsonValue(kTestUserShellApp)));

    TestStory1_Run(0);
  }

  TestPoint story1_run_{"Story1 Run"};

  void TestStory1_Run(const int round) {
    if (!story_controller_) {
      story_provider_->GetController(story_info_->id,
                                     story_controller_.NewRequest());
      story_controller_->GetLink(fidl::Array<fidl::String>::New(0), "root",
                                 root_.NewRequest());
    }

    link_watcher_.Watch(&root_);
    link_watcher_.Continue([this, round] { TestStory1_Cycle(round); });

    story_watcher_.Watch(&story_controller_);
    story_watcher_.Continue([this] {
      story_controller_->Stop([this] {
        TeardownStoryController();
        story1_run_.Pass();

        // When the story is done, we start the next one.
        mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
            [this] { TestStory2(); }, ftl::TimeDelta::FromSeconds(20));
      });
    });

    story_modules_watcher_.Watch(&story_controller_);
    story_links_watcher_.Watch(&story_controller_);

    // Start and show the new story.
    fidl::InterfaceHandle<mozart::ViewOwner> story_view;
    story_controller_->Start(story_view.NewRequest());
    view_->ConnectView(std::move(story_view));

    if (round == 0) {
      story_controller_->AddModule(fidl::Array<fidl::String>::New(0), "second",
                                   settings_.second_module, "root2", nullptr);
    }
  }

  // Every five counter increments, we dehydrate and rehydrate the story, until
  // the story stops itself when it reaches 11 counter increments.
  void TestStory1_Cycle(const int round) {
    story_controller_->GetInfo([this, round](modular::StoryInfoPtr story_info,
                                             modular::StoryState state) {
      FTL_DCHECK(!story_info.is_null());
      FTL_DCHECK(IsRunning(state));
      story_controller_->Stop([this, round] {
        TeardownStoryController();

        // When the story stops, we start it again.
        mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
            [this, round] {
              story_provider_->GetStoryInfo(
                  story_info_->id,
                  [this, round](modular::StoryInfoPtr story_info) {
                    FTL_DCHECK(!story_info.is_null());

                    // Can't use the StoryController here because we closed it
                    // in TeardownStoryController().
                    story_provider_->RunningStories(
                        [this, round](fidl::Array<fidl::String> story_ids) {
                          auto n = count(story_ids.begin(), story_ids.begin(),
                                         story_info_->id);
                          FTL_DCHECK(n == 0);

                          TestStory1_Run(round + 1);
                        });
                  });
            },
            ftl::TimeDelta::FromSeconds(10));
      });
    });
  }

  TestPoint story2_create_{"Story2 Create"};

  void TestStory2() {
    const std::string& url = settings_.second_module;

    modular::JsonDoc doc;
    std::vector<std::string> segments{"example", url, "created-with-info"};
    modular::CreatePointer(doc, segments.begin(), segments.end())
        .Set(doc, true);

    using FidlStringMap = fidl::Map<fidl::String, fidl::String>;
    story_provider_->CreateStoryWithInfo(url, FidlStringMap(),
                                         modular::JsonValueToString(doc),
                                         [this](const fidl::String& story_id) {
                                           story2_create_.Pass();
                                           TestStory2_GetController(story_id);
                                         });
  }

  TestPoint story2_get_controller_{"Story2 Get Controller"};

  void TestStory2_GetController(const fidl::String& story_id) {
    story_provider_->GetController(story_id, story_controller_.NewRequest());
    story_controller_->GetInfo(
        [this](modular::StoryInfoPtr story_info, modular::StoryState state) {
          story_info_ = std::move(story_info);

          story2_get_controller_.Pass();

          TestStory2_GetModules();
        });
  }

  TestPoint story2_get_modules_{"Story2 Get Modules"};

  void TestStory2_GetModules() {
    story_controller_->GetModules(
        [this](fidl::Array<modular::ModuleDataPtr> modules) {
          story2_get_modules_.Pass();

          FTL_LOG(INFO) << "TestUserShell MODULES:";
          for (const auto& module_data : modules) {
            FTL_LOG(INFO) << "TestUserShell MODULE: url=" << module_data->module_url;
            FTL_LOG(INFO) << "TestUserShell         link="
                          << module_data->link_path->link_name;
            std::string path;
            for (const auto& path_element : module_data->module_path) {
              path.push_back(' ');
              path.append(path_element);
            }
            if (path.size()) {
              FTL_LOG(INFO) << "TestUserShell         path=" << path.substr(1);
            }
          }

          TestStory2_Run();
        });
  }

  TestPoint story2_info_before_run_{"Story2 GetInfo before Run"};
  TestPoint story2_run_{"Story2 Run"};

  void TestStory2_Run() {
    story_controller_->GetInfo([this] (modular::StoryInfoPtr info, modular::StoryState state) {
        story2_info_before_run_.Pass();
        FTL_LOG(INFO) << "StoryState before Start(): " << state;

        if (state != modular::StoryState::INITIAL &&
            state != modular::StoryState::STOPPED) {
          modular::testing::Fail("StoryState before Start() must be STARTING or RUNNING.");
        }
      });

    // Start and show the new story.
    fidl::InterfaceHandle<mozart::ViewOwner> story_view;
    story_controller_->Start(story_view.NewRequest());
    view_->ConnectView(std::move(story_view));

    story_controller_->GetInfo([this] (modular::StoryInfoPtr info, modular::StoryState state) {
        story2_run_.Pass();

        FTL_LOG(INFO) << "StoryState after Start(): " << state;

        if (state != modular::StoryState::STARTING &&
            state != modular::StoryState::RUNNING) {
          modular::testing::Fail("StoryState after Start() must be STARTING or RUNNING.");
        }

        mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
            [this] { TestStory2_DeleteStory(); }, ftl::TimeDelta::FromSeconds(20));
      });
  }

  TestPoint story2_delete_{"Story2 Delete"};

  void TestStory2_DeleteStory() {
    story_provider_->DeleteStory(story_info_->id, [this] {
      story2_delete_.Pass();
    });

    story_provider_->GetStoryInfo(story_info_->id, [this](modular::StoryInfoPtr info) {
        TestStory2_InfoAfterDeleteIsNull(std::move(info));
      });
  }

  TestPoint story2_info_after_delete_{"Story2 Info After Delete"};

  void TestStory2_InfoAfterDeleteIsNull(modular::StoryInfoPtr info) {
    story2_info_after_delete_.Pass();
    if (!info.is_null()) {
      modular::testing::Fail("StoryInfo after DeleteStory() must return null.");
    }

    user_context_->Logout();
  }

  TestPoint terminate_{"Terminate"};

  // |UserShell|
  void Terminate(const TerminateCallback& done) override {
    terminate_.Pass();

    // A little acrobatics to allow TestPoints, which are data members of this,
    // to post failure notices to the test runner in their destructors.
    //
    // We respond to done first, then asynchronously delete this, then
    // asynchronously post Teardown() to the test runner, and finally
    // asynchronously stop the message queue.

    auto binding = PassBinding();  // To invoke done() after delete this.

    delete this;

    modular::testing::Done([done] {
      done();
      mtl::MessageLoop::GetCurrent()->PostQuitTask();
    });
  }

  void TeardownStoryController() {
    story_watcher_.Reset();
    story_modules_watcher_.Reset();
    story_links_watcher_.Reset();
    link_watcher_.Reset();
    story_controller_.reset();
    root_.reset();
  }

  const Settings settings_;

  StoryProviderWatcherImpl story_provider_watcher_;
  StoryWatcherImpl story_watcher_;
  StoryModulesWatcherImpl story_modules_watcher_;
  StoryLinksWatcherImpl story_links_watcher_;
  LinkWatcherImpl link_watcher_;

  std::unique_ptr<modular::ViewHost> view_;

  modular::UserContextPtr user_context_;
  modular::UserShellContextPtr user_shell_context_;
  modular::StoryProviderPtr story_provider_;
  modular::StoryControllerPtr story_controller_;
  modular::LinkPtr root_;
  modular::LinkPtr user_shell_link_;
  modular::StoryInfoPtr story_info_;

  FTL_DISALLOW_COPY_AND_ASSIGN(TestUserShellApp);
};

}  // namespace

int main(int argc, const char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  Settings settings(command_line);

  mtl::MessageLoop loop;
  TestUserShellApp::New(settings);
  loop.Run();
  return 0;
}
