// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of a user shell for module development. It takes a
// root module URL and data for its Link as command line arguments,
// which can be set using the device_runner --user-shell-args flag.

#include "application/lib/app/connect.h"
#include "application/services/service_provider.fidl.h"
#include "apps/maxwell/services/suggestion/suggestion_provider.fidl.h"
#include "apps/modular/lib/fidl/single_service_view_app.h"
#include "apps/modular/services/story/link.fidl.h"
#include "apps/modular/services/user/user_context.fidl.h"
#include "apps/modular/services/user/user_shell.fidl.h"
#include "apps/mozart/lib/view_framework/base_view.h"
#include "apps/mozart/services/geometry/cpp/geometry_util.h"
#include "apps/mozart/services/views/view_manager.fidl.h"
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

constexpr uint32_t kRootModuleKey = 1;
constexpr uint32_t kRootViewSceneResourceId = 1;
constexpr uint32_t kRootViewSceneNodeId = 1;

class Settings {
 public:
  explicit Settings(const ftl::CommandLine& command_line) {
    root_module = command_line.GetOptionValueWithDefault(
        "root_module", "file:///system/apps/example_recipe");
    root_link = command_line.GetOptionValueWithDefault("root_link", "");
    story_id = command_line.GetOptionValueWithDefault("story_id", "");
  }

  std::string root_module;
  std::string root_link;
  std::string story_id;
};

struct ViewData {
  mozart::ViewInfoPtr view_info;
  mozart::ViewPropertiesPtr view_properties;
  mozart::RectF layout_bounds;
  uint32_t scene_version = 1u;
};

class DevUserShellView : public mozart::BaseView {
 public:
  explicit DevUserShellView(
      mozart::ViewManagerPtr view_manager,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request)
      : BaseView(std::move(view_manager),
                 std::move(view_owner_request),
                 "DevUserShellView") {}

  ~DevUserShellView() override = default;

  void SetRootModuleView(fidl::InterfaceHandle<mozart::ViewOwner> view_owner) {
    if (view_owner) {
      GetViewContainer()->AddChild(kRootModuleKey, std::move(view_owner));
      root_view_data_.reset(new ViewData);
    } else if (root_view_data_) {
      GetViewContainer()->RemoveChild(kRootModuleKey, nullptr);
      root_view_data_.reset();
      Invalidate();
    }
  }

 private:
  // |BaseView|:
  void OnChildAttached(uint32_t child_key,
                       mozart::ViewInfoPtr child_view_info) override {
    root_view_data_->view_info = std::move(child_view_info);
    Invalidate();
  }

  // |BaseView|:
  void OnChildUnavailable(uint32_t child_key) override {
    root_view_data_.reset();
    GetViewContainer()->RemoveChild(child_key, nullptr);
    Invalidate();
  }

  // |BaseView|:
  void OnLayout() override {
    FTL_DCHECK(properties());
    // Layout root view
    if (root_view_data_) {
      const mozart::Size& size = *properties()->view_layout->size;

      root_view_data_->layout_bounds.x = 0;
      root_view_data_->layout_bounds.y = 0;
      root_view_data_->layout_bounds.width = size.width;
      root_view_data_->layout_bounds.height = size.height;

      auto view_properties = mozart::ViewProperties::New();
      view_properties->view_layout = mozart::ViewLayout::New();
      view_properties->view_layout->size = mozart::Size::New();
      view_properties->view_layout->size->width =
          root_view_data_->layout_bounds.width;
      view_properties->view_layout->size->height =
          root_view_data_->layout_bounds.height;

      if (!root_view_data_->view_properties.Equals(view_properties)) {
        root_view_data_->view_properties = view_properties.Clone();
        root_view_data_->scene_version++;
        GetViewContainer()->SetChildProperties(kRootModuleKey,
                                               root_view_data_->scene_version,
                                               std::move(view_properties));
      }
    }
  }

  // |BaseView|:
  void OnDraw() override {
    FTL_DCHECK(properties());

    // Update the scene.
    // TODO: only send the resources once, be more incremental
    auto update = mozart::SceneUpdate::New();
    update->clear_resources = true;
    update->clear_nodes = true;

    // Create the root node
    auto root_node = mozart::Node::New();

    // If we have the view, add it to the scene.
    if (root_view_data_ && root_view_data_->view_info) {
      mozart::RectF extent;
      extent.width = root_view_data_->layout_bounds.width;
      extent.height = root_view_data_->layout_bounds.height;
      root_node->content_transform = mozart::Transform::New();
      root_node->content_clip = extent.Clone();
      mozart::SetTranslationTransform(root_node->content_transform.get(),
                                      root_view_data_->layout_bounds.x,
                                      root_view_data_->layout_bounds.y, 0.f);

      auto scene_resource = mozart::Resource::New();
      scene_resource->set_scene(mozart::SceneResource::New());
      scene_resource->get_scene()->scene_token =
          root_view_data_->view_info->scene_token.Clone();
      update->resources.insert(kRootViewSceneResourceId,
                               std::move(scene_resource));

      auto scene_node = mozart::Node::New();
      scene_node->op = mozart::NodeOp::New();
      scene_node->op->set_scene(mozart::SceneNodeOp::New());
      scene_node->op->get_scene()->scene_resource_id = kRootViewSceneResourceId;
      update->nodes.insert(kRootViewSceneNodeId, std::move(scene_node));
      root_node->child_node_ids.push_back(kRootViewSceneNodeId);
    }

    // Add the root node.
    update->nodes.insert(mozart::kSceneRootNodeId, std::move(root_node));
    scene()->Update(std::move(update));

    // Publish the scene.
    scene()->Publish(CreateSceneMetadata());
  }

  std::unique_ptr<ViewData> root_view_data_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DevUserShellView);
};

class DevUserShellApp
    : public modular::StoryWatcher,
      public modular::SingleServiceViewApp<modular::UserShell> {
 public:
  explicit DevUserShellApp(const Settings& settings)
      : settings_(settings), story_watcher_binding_(this) {}
  ~DevUserShellApp() override = default;

 private:
  // |SingleServiceViewApp|
  void CreateView(
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      fidl::InterfaceRequest<app::ServiceProvider> services) override {
    view_owner_request_ = std::move(view_owner_request);
    Connect();
  }

  // |UserShell|
  void Initialize(
      fidl::InterfaceHandle<modular::UserContext> user_context,
      fidl::InterfaceHandle<modular::StoryProvider> story_provider,
      fidl::InterfaceHandle<maxwell::SuggestionProvider> suggestion_provider,
      fidl::InterfaceRequest<modular::FocusController> focus_controller_request)
      override {
    user_context_.Bind(std::move(user_context));
    story_provider_.Bind(std::move(story_provider));
    Connect();
  }

  // |UserShell|
  void Terminate(const TerminateCallback& done) override {
    mtl::MessageLoop::GetCurrent()->PostQuitTask();
    done();
  };

  void Connect() {
    if (!view_owner_request_ || !story_provider_) {
      // Not yet ready, wait for the other of CreateView() and
      // Initialize() to be called.
      return;
    }

    FTL_LOG(INFO) << "DevUserShell START " << settings_.root_module << " "
                  << settings_.root_link;

    view_.reset(new DevUserShellView(
        application_context()
            ->ConnectToEnvironmentService<mozart::ViewManager>(),
        std::move(view_owner_request_)));

    if (settings_.story_id.empty()) {
      story_provider_->CreateStory(
          settings_.root_module,
          [this](const fidl::String& story_id) { StartStoryById(story_id); });
    } else {
      StartStoryById(settings_.story_id);
    }
  }

  void StartStoryById(const fidl::String& story_id) {
    story_provider_->GetController(story_id, story_controller_.NewRequest());
    story_controller_.set_connection_error_handler([this, story_id] {
      FTL_LOG(ERROR) << "Story controller for story " << story_id
                     << " died. Does this story exist?";
    });

    story_controller_->Watch(story_watcher_binding_.NewBinding());

    fidl::InterfaceHandle<mozart::ViewOwner> root_module_view;
    story_controller_->Start(root_module_view.NewRequest());
    view_->SetRootModuleView(std::move(root_module_view));

    if (!settings_.root_link.empty()) {
      modular::LinkPtr root;
      story_controller_->GetLink(root.NewRequest());
      root->UpdateObject(nullptr, settings_.root_link);
    }
  }

  // |StoryWatcher|
  void OnStateChange(modular::StoryState state) override {
    if (state != modular::StoryState::DONE) {
      return;
    }

    FTL_LOG(INFO) << "DevUserShell DONE";
    story_controller_->Stop([this] {
      FTL_LOG(INFO) << "DevUserShell STOP";
      story_watcher_binding_.Close();
      story_controller_.reset();
      view_->SetRootModuleView(nullptr);

      user_context_->Logout();
    });
  }

  const Settings settings_;

  fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request_;
  std::unique_ptr<DevUserShellView> view_;

  modular::UserContextPtr user_context_;
  modular::StoryProviderPtr story_provider_;
  modular::StoryControllerPtr story_controller_;
  fidl::Binding<modular::StoryWatcher> story_watcher_binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DevUserShellApp);
};

}  // namespace

int main(int argc, const char** argv) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  Settings settings(command_line);

  mtl::MessageLoop loop;
  DevUserShellApp app(settings);
  loop.Run();
  return 0;
}
