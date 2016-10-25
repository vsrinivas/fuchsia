// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of a dummy User shell.
// This takes |recipe_url| as a command line argument and passes it to the
// Story Manager.

#include <mojo/system/main.h>

#include "apps/modular/mojo/single_service_view_app.h"
#include "apps/modular/story_manager/story_manager.mojom.h"
#include "apps/mozart/lib/view_framework/base_view.h"
#include "apps/mozart/services/views/interfaces/view_provider.mojom.h"
#include "apps/mozart/services/views/interfaces/view_token.mojom.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/synchronization/sleep.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/interface_ptr_set.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "mojo/public/cpp/bindings/strong_binding_set.h"
#include "mojo/public/interfaces/application/service_provider.mojom.h"

namespace modular {

constexpr char kExampleRecipeUrl[] = "mojo:example_recipe";
constexpr char kFlutterModuleUrl[] = "mojo:example_module3.flx";

constexpr uint32_t kRootNodeId = mozart::kSceneRootNodeId;
constexpr uint32_t kViewResourceIdBase = 100;

using mojo::ApplicationImplBase;
using mojo::ConnectionContext;
using mojo::GetProxy;
using mojo::InterfaceHandle;
using mojo::InterfacePtr;
using mojo::InterfaceRequest;
using mojo::ServiceProviderImpl;
using mojo::Shell;
using mojo::StrongBindingSet;
using mojo::StrongBinding;
using mojo::StructPtr;

class DummyUserShellImpl : public UserShell, public mozart::BaseView {
 public:
  explicit DummyUserShellImpl(
      mojo::InterfaceHandle<mojo::ApplicationConnector> app_connector,
      InterfaceRequest<UserShell> user_shell_request,
      InterfaceRequest<mozart::ViewOwner> view_owner_request)
      : BaseView(std::move(app_connector),
                 std::move(view_owner_request),
                 "DummyUserShellImpl"),
        binding_(this, std::move(user_shell_request)),
        child_view_key_(0) {}
  ~DummyUserShellImpl() override{};

 private:
  // |UserShell| override.
  void SetStoryProvider(
      InterfaceHandle<StoryProvider> story_provider) override {
    story_provider_.Bind(std::move(story_provider));

    // Check for previous stories.
    story_provider_->PreviousStories([this](InterfaceHandle<Story> story) {
      FTL_DCHECK(!story.is_valid());
    });

    StartAndEmbedStory(kExampleRecipeUrl);
    story_ptr_.set_connection_error_handler([this] {
      child_view_key_++;
      StartAndEmbedStory(kFlutterModuleUrl);
    });
  }

  void StartAndEmbedStory(const std::string& url) {
    story_provider_->CreateStory(url, GetProxy(&story_ptr_));
    story_ptr_->GetInfo([this](StructPtr<StoryInfo> story_info) {
      FTL_LOG(INFO) << "modular::StoryInfo received with url: "
                    << story_info->url
                    << " is_running: " << story_info->is_running;
    });

    InterfaceHandle<mozart::ViewOwner> story_view;
    story_ptr_->Start(GetProxy(&story_view));

    // Embed the new story.
    GetViewContainer()->AddChild(child_view_key_, std::move(story_view));
  }

  // |mozart::BaseView| override.
  void OnChildAttached(uint32_t child_key,
                       StructPtr<mozart::ViewInfo> child_view_info) override {
    view_info_ = std::move(child_view_info);
    auto view_properties = mozart::ViewProperties::New();
    GetViewContainer()->SetChildProperties(child_view_key_, 0 /* scene_token */,
                                           std::move(view_properties));
    Invalidate();
  }

  // |mozart::BaseView| override.
  void OnChildUnavailable(uint32_t child_key) override {
    view_info_.reset();
    GetViewContainer()->RemoveChild(child_key, nullptr);
    Invalidate();
  }

  // |mozart::BaseView| override.
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

  StrongBinding<UserShell> binding_;
  InterfacePtr<StoryProvider> story_provider_;
  InterfacePtr<Story> story_ptr_;

  StructPtr<mozart::ViewInfo> view_info_;
  uint32_t child_view_key_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DummyUserShellImpl);
};

}  // namespace modular

MojoResult MojoMain(MojoHandle application_request) {
  FTL_LOG(INFO) << "dummy_user_shell main";
  modular::SingleServiceViewApp<modular::UserShell, modular::DummyUserShellImpl>
      app;
  return mojo::RunApplication(application_request, &app);
}
