
// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of the StoryShell service that just lays out the
// views of all modules side by side.

#include <memory>

#include <fuchsia/cpp/modular.h>
#include <fuchsia/cpp/views_v1.h>
#include <fuchsia/cpp/views_v1_token.h>
#include "lib/app/cpp/application_context.h"
#include "lib/app_driver/cpp/app_driver.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "peridot/lib/fidl/single_service_app.h"
#include "peridot/lib/fidl/view_host.h"

namespace {

class DevStoryShellApp : public modular::SingleServiceApp<modular::StoryShell> {
 public:
  DevStoryShellApp(component::ApplicationContext* const application_context)
      : SingleServiceApp(application_context) {}

  ~DevStoryShellApp() override = default;

 private:
  // |SingleServiceApp|
  void CreateView(
      fidl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request,
      fidl::InterfaceRequest<component::ServiceProvider> /*services_request*/)
      override {
    view_owner_request_ = std::move(view_owner_request);
    Connect();
  }

  // |StoryShell|
  void Initialize(
      fidl::InterfaceHandle<modular::StoryContext> story_context) override {
    story_context_.Bind(std::move(story_context));
    Connect();
  }

  // |StoryShell|
  void ConnectView(fidl::InterfaceHandle<views_v1_token::ViewOwner> view_owner,
                   fidl::StringPtr /*view_id*/,
                   fidl::StringPtr /*parent_id*/,
                   modular::SurfaceRelationPtr /*surface_relation*/,
                   modular::ModuleManifestPtr /*module_manifest*/) override {
    if (view_) {
      view_->ConnectView(std::move(view_owner));
    } else {
      child_views_.push_back(std::move(view_owner));
    }
  }

  // |StoryShell|
  void FocusView(fidl::StringPtr /*view_id*/,
                 fidl::StringPtr /*relative_view_id*/) override {}

  // |StoryShell|
  void DefocusView(fidl::StringPtr /*view_id*/,
                   DefocusViewCallback callback) override {
    callback();
  }

  // |StoryShell|
  void AddContainer(
      fidl::StringPtr /*container_name*/,
      fidl::StringPtr /*parent_id*/,
      modular::SurfaceRelation /* relation */,
      fidl::VectorPtr<modular::ContainerLayout> /*layout*/,
      fidl::VectorPtr<modular::ContainerRelationEntry> /* relationships */,
      fidl::VectorPtr<modular::ContainerView> /* views */) override {}

  void Connect() {
    if (story_context_.is_bound() && view_owner_request_) {
      view_ = std::make_unique<modular::ViewHost>(
          application_context()
              ->ConnectToEnvironmentService<views_v1::ViewManager>(),
          std::move(view_owner_request_));

      for (auto& view_owner : child_views_) {
        view_->ConnectView(std::move(view_owner));
      }

      child_views_.clear();
    }
  }

  std::unique_ptr<modular::ViewHost> view_;
  std::vector<fidl::InterfaceHandle<views_v1_token::ViewOwner>> child_views_;

  fidl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request_;
  modular::StoryContextPtr story_context_;
  FXL_DISALLOW_COPY_AND_ASSIGN(DevStoryShellApp);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;

  auto app_context = component::ApplicationContext::CreateFromStartupInfo();
  modular::AppDriver<DevStoryShellApp> driver(
      app_context->outgoing_services(),
      std::make_unique<DevStoryShellApp>(app_context.get()),
      [&loop] { loop.QuitNow(); });

  loop.Run();
  return 0;
}
