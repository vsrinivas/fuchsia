// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of a dummy User shell.
// This takes |recipe_url| as a command line argument and passes it to the
// Story Manager.

#include <memory>

#include "apps/maxwell/services/suggestion/suggestion_provider.fidl.h"
#include "apps/modular/lib/app/connect.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/fidl/single_service_view_app.h"
#include "apps/modular/services/application/service_provider.fidl.h"
#include "apps/modular/services/user/user_shell.fidl.h"
#include "apps/mozart/lib/view_framework/base_view.h"
#include "apps/mozart/services/views/view_manager.fidl.h"
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/tasks/task_runner.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

constexpr uint32_t kRootNodeId = mozart::kSceneRootNodeId;
constexpr uint32_t kViewResourceIdBase = 100;

class Settings {
 public:
  explicit Settings(const ftl::CommandLine& command_line) {
    // Good other value to use for dev:
    // "file:///system/apps/example_flutter_counter_parent"
    first_module = command_line.GetOptionValueWithDefault(
        "first-module", "file:///system/apps/example_recipe");
    second_module = command_line.GetOptionValueWithDefault(
        "second-module", "file:///system/apps/example_flutter_hello_world");
  }

  std::string first_module;
  std::string second_module;
};

class DummyUserShellView : public mozart::BaseView {
 public:
  explicit DummyUserShellView(
      mozart::ViewManagerPtr view_manager,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request)
      : BaseView(std::move(view_manager),
                 std::move(view_owner_request),
                 "DummyUserShellView") {}

  ~DummyUserShellView() override = default;

  void ConnectView(fidl::InterfaceHandle<mozart::ViewOwner> view_owner) {
    GetViewContainer()->AddChild(++child_view_key_, std::move(view_owner));
  }

 private:
  // |mozart::BaseView|
  void OnChildAttached(uint32_t child_key,
                       mozart::ViewInfoPtr child_view_info) override {
    view_info_ = std::move(child_view_info);
    auto view_properties = mozart::ViewProperties::New();
    GetViewContainer()->SetChildProperties(child_view_key_, 0 /* scene_token */,
                                           std::move(view_properties));
    Invalidate();
  }

  // |mozart::BaseView|
  void OnChildUnavailable(uint32_t child_key) override {
    view_info_.reset();
    GetViewContainer()->RemoveChild(child_key, nullptr);
    Invalidate();
  }

  // |mozart::BaseView|
  void OnDraw() override {
    FTL_DCHECK(properties());

    auto update = mozart::SceneUpdate::New();
    auto root_node = mozart::Node::New();

    if (view_info_) {
      const uint32_t scene_resource_id = kViewResourceIdBase + child_view_key_;
      auto scene_resource = mozart::Resource::New();
      scene_resource->set_scene(mozart::SceneResource::New());
      scene_resource->get_scene()->scene_token =
          view_info_->scene_token.Clone();
      update->resources.insert(scene_resource_id, std::move(scene_resource));
      root_node->op = mozart::NodeOp::New();
      root_node->op->set_scene(mozart::SceneNodeOp::New());
      root_node->op->get_scene()->scene_resource_id = scene_resource_id;
    }

    update->nodes.insert(kRootNodeId, std::move(root_node));
    scene()->Update(std::move(update));
    scene()->Publish(CreateSceneMetadata());
  }

  mozart::ViewInfoPtr view_info_;
  uint32_t child_view_key_{};

  FTL_DISALLOW_COPY_AND_ASSIGN(DummyUserShellView);
};

class DummyUserShellApp
    : public modular::StoryWatcher,
      public modular::StoryProviderWatcher,
      public modular::SingleServiceViewApp<modular::UserShell> {
 public:
  explicit DummyUserShellApp(const Settings& settings)
      : settings_(settings),
        story_provider_watcher_binding_(this),
        story_watcher_binding_(this) {}
  ~DummyUserShellApp() override = default;

 private:
  // |SingleServiceViewApp|
  void CreateView(
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      fidl::InterfaceRequest<modular::ServiceProvider> services) override {
    view_.reset(new DummyUserShellView(
        application_context()
            ->ConnectToEnvironmentService<mozart::ViewManager>(),
        std::move(view_owner_request)));
  }

  // |UserShell|
  void Initialize(fidl::InterfaceHandle<modular::StoryProvider> story_provider,
                  fidl::InterfaceHandle<maxwell::suggestion::SuggestionProvider>
                      suggestion_provider,
                  fidl::InterfaceRequest<modular::FocusController>
                      focus_controller_request) override {
    story_provider_.Bind(std::move(story_provider));

    fidl::InterfaceHandle<modular::StoryProviderWatcher> watcher;
    story_provider_watcher_binding_.Bind(GetProxy(&watcher));
    story_provider_->Watch(std::move(watcher));

    story_provider_->PreviousStories([this](fidl::Array<fidl::String> stories) {
      if (stories.size() > 0) {
        std::shared_ptr<unsigned int> count = std::make_shared<unsigned int>(0);
        for (auto& story_id : stories) {
          story_provider_->GetStoryInfo(story_id, [
            this, story_id, count, max = stories.size()
          ](modular::StoryInfoPtr story_info) {
            ++*count;
            FTL_LOG(INFO) << "Previous story " << *count << " of " << max << " "
                          << story_id << " " << story_info->url;

            if (*count == max) {
              CreateStory(settings_.first_module);
            }
          });
        }
      } else {
        CreateStory(settings_.first_module);
      }
    });
  }

  // |StoryProviderWatcher|
  void OnDelete(const ::fidl::String& story_id) override {
    FTL_VLOG(1) << "DummyUserShellApp::OnDelete() " << story_id;
  }

  // |StoryProviderWatcher|
  void OnChange(modular::StoryInfoPtr story_info) override {
    FTL_VLOG(1) << "DummyUserShellApp::OnChange() "
                << " id " << story_info->id << " is_running "
                << story_info->is_running << " state " << story_info->state
                << " url " << story_info->url;
  }

  // |StoryWatcher|
  void OnStart() override {}

  // |StoryWatcher|
  void OnData() override {
    if (++data_count_ % 5 == 0) {
      StopExampleStory();
    }
  }

  // |StoryWatcher|
  void OnStop() override {}

  // |StoryWatcher|
  void OnError() override {}

  // |StoryWatcher|
  void OnDone() override {
    FTL_LOG(INFO) << "DummyUserShell DONE";
    story_controller_->Stop([this]() {
      TearDownStoryController();

      // When the story is done, we start the next one.
      mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
          [this]() { CreateStory(settings_.second_module); },
          ftl::TimeDelta::FromSeconds(20));
    });
  }

  void CreateStory(const fidl::String& url) {
    story_provider_->CreateStory(url, fidl::GetProxy(&story_controller_));
    story_controller_->GetInfo([this](modular::StoryInfoPtr story_info) {
      story_info_ = std::move(story_info);
      FTL_LOG(INFO) << "DummyUserShell START " << story_info_->id << " "
                    << story_info_->url;
      InitStory();
    });
  }

  void ResumeStory() {
    FTL_LOG(INFO) << "DummyUserShell RESUME";
    story_provider_->ResumeStory(story_info_->id,
                                 fidl::GetProxy(&story_controller_));
    InitStory();
  }

  void InitStory() {
    fidl::InterfaceHandle<StoryWatcher> story_watcher;
    story_watcher_binding_.Bind(fidl::GetProxy(&story_watcher));
    story_controller_->Watch(std::move(story_watcher));

    fidl::InterfaceHandle<mozart::ViewOwner> story_view;
    story_controller_->Start(fidl::GetProxy(&story_view));

    // Show the new story, if we have a view.
    if (view_) {
      view_->ConnectView(std::move(story_view));
    }
  }

  void TearDownStoryController() {
    story_watcher_binding_.Close();
    story_controller_.reset();
  }

  // Every five counter increments, we dehydrate and rehydrate the story.
  void StopExampleStory() {
    FTL_LOG(INFO) << "DummyUserShell STOP";

    story_provider_->GetStoryInfo(
        story_info_->id, [this](modular::StoryInfoPtr story_info) {
          FTL_DCHECK(story_info->is_running == true);
          story_controller_->Stop([this]() {
            TearDownStoryController();

            // When the story stops, we start it again.
            mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
                [this]() {
                  story_provider_->GetStoryInfo(
                      story_info_->id,
                      [this](modular::StoryInfoPtr story_info) {
                        FTL_DCHECK(story_info->is_running == false);
                        ResumeStory();
                      });
                },
                ftl::TimeDelta::FromSeconds(10));
          });
        });
  }

  const Settings settings_;
  fidl::Binding<modular::StoryProviderWatcher> story_provider_watcher_binding_;
  fidl::Binding<modular::StoryWatcher> story_watcher_binding_;
  std::unique_ptr<DummyUserShellView> view_;
  modular::StoryProviderPtr story_provider_;
  modular::StoryControllerPtr story_controller_;
  modular::StoryInfoPtr story_info_;
  int data_count_{0};

  FTL_DISALLOW_COPY_AND_ASSIGN(DummyUserShellApp);
};

}  // namespace

int main(int argc, const char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  Settings settings(command_line);

  mtl::MessageLoop loop;
  DummyUserShellApp app(settings);
  loop.Run();
  return 0;
}
