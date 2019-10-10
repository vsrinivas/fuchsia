// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/ui/base_view/cpp/base_view.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/eventpair.h>

#include <array>
#include <memory>

#include "src/lib/component/cpp/startup_context.h"
#include "src/lib/fxl/logging.h"
#include "src/modular/lib/app_driver/cpp/app_driver.h"
#include "src/modular/lib/fidl/single_service_app.h"
#include "trace-provider/provider.h"

namespace {

constexpr int kSwapSeconds = 5;
constexpr std::array<const char*, 2> kModuleQueries{{"swap_module1", "swap_module2"}};

class RecipeView : public scenic::BaseView {
 public:
  explicit RecipeView(scenic::ViewContext view_context)
      : BaseView(std::move(view_context), "RecipeView") {}

  ~RecipeView() override = default;

  void SetChild(fuchsia::ui::views::ViewHolderToken view_holder_token) {
    if (host_node_) {
      host_node_->Detach();
      host_node_.reset();
      host_view_holder_.reset();
    }

    if (view_holder_token.value) {
      host_node_ = std::make_unique<scenic::EntityNode>(session());
      host_view_holder_ =
          std::make_unique<scenic::ViewHolder>(session(), std::move(view_holder_token), "Swap");

      host_node_->SetTranslation(0.f, 0.f, -0.1f);
      host_node_->Attach(*host_view_holder_);
      root_node().AddChild(*host_node_);
    }
  }

 private:
  // |scenic::SessionListener|
  void OnScenicError(std::string error) override { FXL_LOG(ERROR) << "Scenic Error " << error; }

  // |scenic::BaseView|
  void OnPropertiesChanged(fuchsia::ui::gfx::ViewProperties) override {
    if (host_node_) {
      auto child_properties = fuchsia::ui::gfx::ViewProperties::New();
      fidl::Clone(view_properties(), child_properties.get());
      host_view_holder_->SetViewProperties(*child_properties);
    }
  }

  std::unique_ptr<scenic::EntityNode> host_node_;
  std::unique_ptr<scenic::ViewHolder> host_view_holder_;
};

class RecipeApp : public modular::ViewApp {
 public:
  RecipeApp(sys::ComponentContext* const component_context)
      : ViewApp(component_context),
        component_context_(std::unique_ptr<sys::ComponentContext>(component_context)) {
    component_context->svc()->Connect(module_context_.NewRequest());
    SwapModule();
  }

  ~RecipeApp() override = default;

 private:
  // |ViewApp|
  void CreateView(zx::eventpair view_token,
                  fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
                  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services) override {
    auto scenic = component_context()->svc()->Connect<fuchsia::ui::scenic::Scenic>();
    scenic::ViewContext view_context = {
        .session_and_listener_request =
            scenic::CreateScenicSessionPtrAndListenerRequest(scenic.get()),
        .view_token = scenic::ToViewToken(std::move(view_token)),
        .incoming_services = std::move(incoming_services),
        .outgoing_services = std::move(outgoing_services),
        .component_context = component_context_.get(),
    };
    view_ = std::make_unique<RecipeView>(std::move(view_context));
    SetChild();
  }

  void SwapModule() {
    StartModule(kModuleQueries[query_index_]);
    query_index_ = (query_index_ + 1) % kModuleQueries.size();
    async::PostDelayedTask(
        async_get_default_dispatcher(), [this] { SwapModule(); }, zx::sec(kSwapSeconds));
  }

  void StartModule(const std::string& module_query) {
    if (module_) {
      module_->Stop([this, module_query] {
        module_.Unbind();
        view_holder_token_.value.reset();
        StartModule(module_query);
      });
      return;
    }

    // This module is named after its URL.
    auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
    fuchsia::modular::Intent intent;
    intent.handler = module_query;
    module_context_->EmbedModule(module_query, std::move(intent), module_.NewRequest(),
                                 std::move(view_token),
                                 [](const fuchsia::modular::StartModuleStatus&) {});
    view_holder_token_ = std::move(view_holder_token);
    SetChild();
  }

  void SetChild() {
    if (view_ && view_holder_token_.value) {
      view_->SetChild(std::move(view_holder_token_));
    }
  }

  fuchsia::modular::ModuleContextPtr module_context_;
  fuchsia::modular::ModuleControllerPtr module_;
  fuchsia::ui::views::ViewHolderToken view_holder_token_;
  std::unique_ptr<sys::ComponentContext> component_context_;
  std::unique_ptr<RecipeView> view_;

  int query_index_ = 0;
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());

  auto context = sys::ComponentContext::Create();
  modular::AppDriver<RecipeApp> driver(
      context->outgoing(), std::make_unique<RecipeApp>(context.get()), [&loop] { loop.Quit(); });

  loop.Run();
  return 0;
}
