
// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of the StoryShell service that just lays out the
// views of all modules side by side.

#include <memory>

#include "lib/app/cpp/application_context.h"
#include "lib/app_driver/cpp/app_driver.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/story/fidl/story_shell.fidl.h"
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
      f1dl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      f1dl::InterfaceRequest<component::ServiceProvider> /*services_request*/)
      override {
    view_owner_request_ = std::move(view_owner_request);
    Connect();
  }

  // |StoryShell|
  void Initialize(
      f1dl::InterfaceHandle<modular::StoryContext> story_context) override {
    story_context_.Bind(std::move(story_context));
    Connect();
  }

  // |StoryShell|
  void ConnectView(f1dl::InterfaceHandle<mozart::ViewOwner> view_owner,
                   const f1dl::String& /*view_id*/,
                   const f1dl::String& /*parent_id*/,
                   modular::SurfaceRelationPtr /*surface_relation*/,
                   modular::ModuleManifestPtr /*module_manifest*/) override {
    if (view_) {
      view_->ConnectView(std::move(view_owner));
    } else {
      child_views_.push_back(std::move(view_owner));
    }
  }

  // |StoryShell|
  void FocusView(const f1dl::String& /*view_id*/,
                 const f1dl::String& /*relative_view_id*/) override {}

  // |StoryShell|
  void DefocusView(const f1dl::String& /*view_id*/,
                   const DefocusViewCallback& callback) override {
    callback();
  }

  // |StoryShell|
  void AddContainer(
      const f1dl::String& /*container_name*/,
      const f1dl::String& /*parent_id*/,
      modular::SurfaceRelationPtr /* relation */,
      f1dl::Array<modular::ContainerLayoutPtr> /*layout*/,
      f1dl::Array<modular::ContainerRelationEntryPtr> /* relationships */,
      f1dl::Array<modular::ContainerViewPtr> /* views */) override {}

  void Connect() {
    if (story_context_.is_bound() && view_owner_request_) {
      view_ = std::make_unique<modular::ViewHost>(
          application_context()
              ->ConnectToEnvironmentService<mozart::ViewManager>(),
          std::move(view_owner_request_));

      for (auto& view_owner : child_views_) {
        view_->ConnectView(std::move(view_owner));
      }

      child_views_.clear();
    }
  }

  std::unique_ptr<modular::ViewHost> view_;
  std::vector<f1dl::InterfaceHandle<mozart::ViewOwner>> child_views_;

  f1dl::InterfaceRequest<mozart::ViewOwner> view_owner_request_;
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
