// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>

#include "apps/modular/lib/fidl/single_service_view_app.h"
#include "apps/modular/services/module/module.fidl.h"
#include "apps/mozart/lib/view_framework/base_view.h"
#include "apps/mozart/services/geometry/cpp/geometry_util.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

constexpr uint32_t kChildKey = 1;
constexpr uint32_t kChildSceneResourceId = 1;
constexpr uint32_t kChildSceneNodeId = 1;
constexpr int kSwapSeconds = 5;
constexpr std::array<const char*, 2> kModuleQueries{
    {"file:///system/apps/swap_module1", "file:///system/apps/swap_module2"}};

class RecipeView : public mozart::BaseView {
 public:
  explicit RecipeView(
      mozart::ViewManagerPtr view_manager,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request)
      : BaseView(std::move(view_manager),
                 std::move(view_owner_request),
                 "RecipeView") {}

  ~RecipeView() override = default;

  void SetChild(mozart::ViewOwnerPtr view_owner) {
    if (view_info_) {
      GetViewContainer()->RemoveChild(kChildKey, nullptr);
    }
    GetViewContainer()->AddChild(kChildKey, std::move(view_owner));
  }

 private:
  // |BaseView|:
  void OnChildAttached(uint32_t child_key,
                       mozart::ViewInfoPtr child_view_info) override {
    view_info_ = std::move(child_view_info);
    Invalidate();
  }

  // |BaseView|:
  void OnLayout() override {
    FTL_DCHECK(properties());
    const mozart::Size& size = *properties()->view_layout->size;
    if (view_info_ && size.width > 0 && size.height > 0) {
      auto view_properties = mozart::ViewProperties::New();
      view_properties->view_layout = mozart::ViewLayout::New();
      view_properties->view_layout->size = mozart::Size::New();
      view_properties->view_layout->size->width = size.width;
      view_properties->view_layout->size->height = size.height;
      view_properties->view_layout->inset = mozart::Inset::New();
      GetViewContainer()->SetChildProperties(kChildKey, 0,
                                             std::move(view_properties));
    }
  }

  // |BaseView|:
  void OnDraw() override {
    auto update = mozart::SceneUpdate::New();
    update->clear_resources = true;
    update->clear_nodes = true;
    auto root_node = mozart::Node::New();

    FTL_DCHECK(properties());
    const mozart::Size& size = *properties()->view_layout->size;
    if (view_info_ && size.width > 0 && size.height > 0) {
      mozart::RectF bounds;
      bounds.width = size.width;
      bounds.height = size.height;
      root_node->content_transform = mozart::Transform::New();
      root_node->content_clip = bounds.Clone();
      mozart::SetTranslationTransform(root_node->content_transform.get(), 0, 0,
                                      0);

      auto scene_resource = mozart::Resource::New();
      scene_resource->set_scene(mozart::SceneResource::New());
      scene_resource->get_scene()->scene_token =
          view_info_->scene_token.Clone();
      update->resources.insert(kChildSceneResourceId,
                               std::move(scene_resource));

      auto scene_node = mozart::Node::New();
      scene_node->op = mozart::NodeOp::New();
      scene_node->op->set_scene(mozart::SceneNodeOp::New());
      scene_node->op->get_scene()->scene_resource_id = kChildSceneResourceId;
      update->nodes.insert(kChildSceneNodeId, std::move(scene_node));
      root_node->child_node_ids.push_back(kChildSceneNodeId);
    }

    update->nodes.insert(mozart::kSceneRootNodeId, std::move(root_node));
    scene()->Update(std::move(update));
    scene()->Publish(CreateSceneMetadata());
  }

  mozart::ViewInfoPtr view_info_;
};

class RecipeApp : public modular::SingleServiceViewApp<modular::Module> {
 public:
  RecipeApp() = default;
  ~RecipeApp() override = default;

 private:
  // |SingleServiceViewApp|
  void CreateView(
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      fidl::InterfaceRequest<app::ServiceProvider> services) override {
    view_.reset(
        new RecipeView(application_context()
                           ->ConnectToEnvironmentService<mozart::ViewManager>(),
                       std::move(view_owner_request)));
    SetChild();
  }

  // |Module|
  void Initialize(
      fidl::InterfaceHandle<modular::ModuleContext> module_context,
      fidl::InterfaceHandle<app::ServiceProvider> incoming_services,
      fidl::InterfaceRequest<app::ServiceProvider> outgoing_services) override {
    module_context_.Bind(std::move(module_context));
    SwapModule();
  }

  // |Module|
  void Stop(const StopCallback& done) override { done(); }

  void SwapModule() {
    StartModule(kModuleQueries[query_index_]);
    query_index_ = (query_index_ + 1) % kModuleQueries.size();
    mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        [this] { SwapModule(); }, ftl::TimeDelta::FromSeconds(kSwapSeconds));
  }

  void StartModule(const std::string& module_query) {
    if (module_) {
      module_->Stop([this, module_query] {
        module_.reset();
        module_view_.reset();
        StartModule(module_query);
      });
      return;
    }

    // This module is named after its URL.
    constexpr char kModuleLink[] = "module";
    module_context_->StartModule(
        module_query, module_query, kModuleLink, nullptr,
        nullptr, module_.NewRequest(), module_view_.NewRequest());
    SetChild();
  }

  void SetChild() {
    if (view_ && module_view_) {
      view_->SetChild(std::move(module_view_));
    }
  }

  modular::ModuleContextPtr module_context_;
  modular::ModuleControllerPtr module_;
  mozart::ViewOwnerPtr module_view_;
  std::unique_ptr<RecipeView> view_;

  int query_index_ = 0;
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  RecipeApp app;
  loop.Run();
  return 0;
}
