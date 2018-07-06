// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <memory>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/app/cpp/startup_context.h>
#include <lib/app_driver/cpp/app_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/ui/view_framework/base_view.h>

#include "peridot/lib/fidl/single_service_app.h"

namespace {

constexpr uint32_t kChildKey = 1;
constexpr int kSwapSeconds = 5;
constexpr std::array<const char*, 2> kModuleQueries{
    {"swap_module1", "swap_module2"}};

class RecipeView : public mozart::BaseView {
 public:
  explicit RecipeView(
      fuchsia::ui::views_v1::ViewManagerPtr view_manager,
      fidl::InterfaceRequest<fuchsia::ui::views_v1_token::ViewOwner>
          view_owner_request)
      : BaseView(std::move(view_manager), std::move(view_owner_request),
                 "RecipeView") {}

  ~RecipeView() override = default;

  void SetChild(fuchsia::ui::views_v1_token::ViewOwnerPtr view_owner) {
    if (host_node_) {
      GetViewContainer()->RemoveChild(kChildKey, nullptr);
      host_node_->Detach();
      host_node_.reset();
    }

    if (view_owner) {
      host_node_ = std::make_unique<scenic::EntityNode>(session());

      zx::eventpair host_import_token;
      host_node_->ExportAsRequest(&host_import_token);
      parent_node().AddChild(*host_node_);

      GetViewContainer()->AddChild(kChildKey, std::move(view_owner),
                                   std::move(host_import_token));
    }
  }

 private:
  // |BaseView|:
  void OnPropertiesChanged(fuchsia::ui::views_v1::ViewProperties) override {
    if (host_node_) {
      auto child_properties = fuchsia::ui::views_v1::ViewProperties::New();
      fidl::Clone(properties(), child_properties.get());
      GetViewContainer()->SetChildProperties(kChildKey,
                                             std::move(child_properties));
    }
  }

  std::unique_ptr<scenic::EntityNode> host_node_;
};

class RecipeApp : public modular::ViewApp {
 public:
  RecipeApp(fuchsia::sys::StartupContext* const startup_context)
      : ViewApp(startup_context) {
    startup_context->ConnectToEnvironmentService(module_context_.NewRequest());
    SwapModule();
  }

  ~RecipeApp() override = default;

 private:
  // |SingleServiceApp|
  void CreateView(
      fidl::InterfaceRequest<fuchsia::ui::views_v1_token::ViewOwner>
          view_owner_request,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> /*services*/)
      override {
    view_ = std::make_unique<RecipeView>(
        startup_context()
            ->ConnectToEnvironmentService<fuchsia::ui::views_v1::ViewManager>(),
        std::move(view_owner_request));
    SetChild();
  }

  void SwapModule() {
    StartModule(kModuleQueries[query_index_]);
    query_index_ = (query_index_ + 1) % kModuleQueries.size();
    async::PostDelayedTask(async_get_default(), [this] { SwapModule(); },
                           zx::sec(kSwapSeconds));
  }

  void StartModule(const std::string& module_query) {
    if (module_) {
      module_->Stop([this, module_query] {
        module_.Unbind();
        module_view_.Unbind();
        StartModule(module_query);
      });
      return;
    }

    // This module is named after its URL.
    fuchsia::modular::Intent intent;
    intent.handler = module_query;
    module_context_->EmbedModule(
        module_query, std::move(intent), module_.NewRequest(),
        module_view_.NewRequest(),
        [](const fuchsia::modular::StartModuleStatus&) {});
    SetChild();
  }

  void SetChild() {
    if (view_ && module_view_) {
      view_->SetChild(std::move(module_view_));
    }
  }

  fuchsia::modular::ModuleContextPtr module_context_;
  fuchsia::modular::ModuleControllerPtr module_;
  fuchsia::ui::views_v1_token::ViewOwnerPtr module_view_;
  std::unique_ptr<RecipeView> view_;

  int query_index_ = 0;
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  auto context = fuchsia::sys::StartupContext::CreateFromStartupInfo();
  modular::AppDriver<RecipeApp> driver(
      context->outgoing().deprecated_services(),
      std::make_unique<RecipeApp>(context.get()), [&loop] { loop.Quit(); });

  loop.Run();
  return 0;
}
