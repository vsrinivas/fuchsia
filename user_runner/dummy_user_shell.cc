// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of a dummy User shell.
// This takes |recipe_url| as a command line argument and passes it to the
// Story Manager.

#include <mojo/system/main.h>

#include "apps/modular/lib/app/connect.h"
#include "apps/modular/mojo/array_to_string.h"
#include "apps/modular/mojo/single_service_view_app.h"
#include "apps/modular/mojo/strong_binding.h"
#include "apps/modular/services/application/service_provider.fidl.h"
#include "apps/modular/services/user/user_runner.fidl.h"
#include "apps/modular/services/user/user_shell.fidl.h"
#include "apps/mozart/lib/view_framework/base_view.h"
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "apps/mozart/services/views/view_manager.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/tasks/task_runner.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/mtl/tasks/message_loop.h"

namespace modular {

constexpr char kExampleRecipeUrl[] = "mojo:example_recipe";
constexpr char kFlutterModuleUrl[] = "mojo:example_module3.flx";

constexpr uint32_t kRootNodeId = mozart::kSceneRootNodeId;
constexpr uint32_t kViewResourceIdBase = 100;

class DummyUserShellImpl : public UserShell,
                           public StoryWatcher,
                           public mozart::BaseView {
 public:
  explicit DummyUserShellImpl(
      mozart::ViewManagerPtr view_manager,
      fidl::InterfaceRequest<UserShell> user_shell_request,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request)
      : BaseView(std::move(view_manager),
                 std::move(view_owner_request),
                 "DummyUserShellImpl"),
        binding_(this, std::move(user_shell_request)),
        story_watcher_binding_(this) {}

  ~DummyUserShellImpl() override = default;

 private:
  // |UserShell|
  void SetStoryProvider(
      fidl::InterfaceHandle<StoryProvider> story_provider) override {
    story_provider_.Bind(std::move(story_provider));
    CreateStory(kExampleRecipeUrl);
  }

  // |StoryWatcher|
  void OnStart() override { FTL_LOG(INFO) << "DummyUserShell::OnStart()"; }

  // |StoryWatcher|
  void OnData() override {
    FTL_LOG(INFO) << "DummyUserShell::OnData() " << ++data_count_;

    // When some data has arrived, we stop the story.
    if (data_count_ % 5 == 0) {
      FTL_LOG(INFO) << "DummyUserShell::OnData() Story.Stop()";
      story_->Stop();
    }
  }

  // |StoryWatcher|
  void OnStop() override {
    FTL_LOG(INFO) << "DummyUserShell::OnStop()";
    TearDownStory();

    // When the story stops, we start it again. HACK(mesch): Right now
    // we don't know when the story is fully torn down and written to
    // ledger. We just wait for 10 seconds and resume it then.
    FTL_LOG(INFO) << "DummyUserShell::OnStop() WAIT for 10s";
    mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        [this]() {
          FTL_LOG(INFO) << "DummyUserShell::OnStop() DONE WAIT for 10s";
          child_view_key_++;
          ResumeStory();
        },
        ftl::TimeDelta::FromSeconds(10));
  }

  // |StoryWatcher|
  void OnDone() override {
    FTL_LOG(INFO) << "DummyUserShell::OnDone()";
    TearDownStory();

    // When the story is done, we start the next one.
    FTL_LOG(INFO) << "DummyUserShell::OnDone() WAIT for 10s";
    mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        [this]() {
          FTL_LOG(INFO) << "DummyUserShell::OnDone() DONE WAIT for 10s";
          child_view_key_++;
          CreateStory(kFlutterModuleUrl);
        },
        ftl::TimeDelta::FromSeconds(10));
  }

  // |mozart::BaseView|
  void OnChildAttached(uint32_t child_key,
                       fidl::StructPtr<mozart::ViewInfo> child_view_info) override {
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

 private:
  void CreateStory(const fidl::String& url) {
    FTL_LOG(INFO) << "DummyUserShell::CreateStory() " << url;
    story_provider_->CreateStory(url, fidl::GetProxy(&story_));
    story_->GetInfo([this](fidl::StructPtr<StoryInfo> story_info) {
      FTL_LOG(INFO) << "DummyUserShell::CreateStory() Story.Getinfo()"
                    << " url: " << story_info->url << " id: " << story_info->id
                    << " session_page_id: "
                    << to_string(story_info->session_page_id)
                    << " is_running: " << story_info->is_running;

      // Retain the story info so we can resume it by ID.
      story_info_ = std::move(story_info);

      InitStory();
    });
  }

  void ResumeStory() {
    FTL_LOG(INFO) << "DummyUserShell::ResumeStory() "
                  << " url: " << story_info_->url << " id: " << story_info_->id
                  << " session_page_id: "
                  << to_string(story_info_->session_page_id)
                  << " is_running: " << story_info_->is_running;

    story_provider_->ResumeStoryByInfo(story_info_->Clone(), fidl::GetProxy(&story_));
    InitStory();
  }

  void InitStory() {
    fidl::InterfaceHandle<StoryWatcher> story_watcher;
    story_watcher_binding_.Bind(fidl::GetProxy(&story_watcher));
    story_->Watch(std::move(story_watcher));

    fidl::InterfaceHandle<mozart::ViewOwner> story_view;
    story_->Start(fidl::GetProxy(&story_view));

    // Embed the new story.
    GetViewContainer()->AddChild(child_view_key_, std::move(story_view));
  }

  void TearDownStory() { story_watcher_binding_.Close(); }

  StrongBinding<UserShell> binding_;
  fidl::Binding<StoryWatcher> story_watcher_binding_;
  StoryProviderPtr story_provider_;
  StoryPtr story_;
  StoryInfoPtr story_info_;
  int data_count_ = 0;

  mozart::ViewInfoPtr view_info_;
  uint32_t child_view_key_ = 0;

  FTL_DISALLOW_COPY_AND_ASSIGN(DummyUserShellImpl);
};

}  // namespace modular

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  modular::SingleServiceViewApp<modular::UserShell, modular::DummyUserShellImpl>
      app;
  loop.Run();
  return 0;
}
