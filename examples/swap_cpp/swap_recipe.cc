// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <memory>

#include "apps/modular/lib/fidl/single_service_app.h"
#include "apps/modular/services/module/module.fidl.h"
#include "lib/ui/view_framework/base_view.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

constexpr uint32_t kChildKey = 1;
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
    if (host_node_) {
      GetViewContainer()->RemoveChild(kChildKey, nullptr);
      host_node_->Detach();
      host_node_.reset();
    }

    if (view_owner) {
      host_node_ = std::make_unique<scenic_lib::EntityNode>(session());

      mx::eventpair host_import_token;
      host_node_->ExportAsRequest(&host_import_token);
      parent_node().AddChild(*host_node_);

      GetViewContainer()->AddChild(kChildKey, std::move(view_owner),
                                   std::move(host_import_token));
    }
  }

 private:
  // |BaseView|:
  void OnPropertiesChanged(
      mozart::ViewPropertiesPtr /*old_properties*/) override {
    if (host_node_) {
      GetViewContainer()->SetChildProperties(kChildKey, properties()->Clone());
    }
  }

  std::unique_ptr<scenic_lib::EntityNode> host_node_;
};

class RecipeApp : public modular::SingleServiceApp<modular::Module> {
 public:
  RecipeApp() = default;
  ~RecipeApp() override = default;

 private:
  // |SingleServiceApp|
  void CreateView(
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      fidl::InterfaceRequest<app::ServiceProvider> /*services*/) override {
    view_ = std::make_unique<RecipeView>(
        application_context()
            ->ConnectToEnvironmentService<mozart::ViewManager>(),
        std::move(view_owner_request));
    SetChild();
  }

  // |Module|
  void Initialize(
      fidl::InterfaceHandle<modular::ModuleContext> module_context,
      fidl::InterfaceHandle<app::ServiceProvider> /*incoming_services*/,
      fidl::InterfaceRequest<app::ServiceProvider> /*outgoing_services*/)
      override {
    module_context_.Bind(std::move(module_context));
    SwapModule();
  }

  // |Lifecycle|
  void Terminate() override { mtl::MessageLoop::GetCurrent()->QuitNow(); }

  void SwapModule() {
    StartModule(kModuleQueries[query_index_]);
    query_index_ = (query_index_ + 1) % kModuleQueries.size();
    mtl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        [this] { SwapModule(); }, fxl::TimeDelta::FromSeconds(kSwapSeconds));
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
    module_context_->StartModule(module_query, module_query, kModuleLink,
                                 nullptr, nullptr, module_.NewRequest(),
                                 module_view_.NewRequest());
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

int main(int /*argc*/, const char** /*argv*/) {
  mtl::MessageLoop loop;
  RecipeApp app;
  loop.Run();
  return 0;
}
