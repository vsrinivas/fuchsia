// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of a user shell for module development. It takes a
// root module URL and data for its Link as command line arguments,
// which can be set using the device_runner --user-shell-args flag.

#include "apps/maxwell/services/suggestion/suggestion_provider.fidl.h"
#include "apps/modular/lib/app/connect.h"
#include "apps/modular/lib/fidl/array_to_string.h"
#include "apps/modular/lib/fidl/single_service_view_app.h"
#include "apps/modular/services/application/service_provider.fidl.h"
#include "apps/modular/services/document_store/document.fidl.h"
#include "apps/modular/services/story/link.fidl.h"
#include "apps/modular/services/user/user_shell.fidl.h"
#include "apps/modular/src/user_runner/link_json.h"
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
#include "third_party/rapidjson/rapidjson/document.h"

namespace {

class Settings {
 public:
  explicit Settings(const ftl::CommandLine& command_line) {
    root_module = command_line.GetOptionValueWithDefault(
        "root-module", "file:///system/apps/example_recipe");
    root_link = command_line.GetOptionValueWithDefault(
        "root-link", "");
  }

  std::string root_module;
  std::string root_link;
};

class DevUserShellApp
    : public modular::StoryWatcher,
      public modular::SingleServiceViewApp<modular::UserShell> {
 public:
  explicit DevUserShellApp(const Settings& settings)
      : settings_(settings),
        story_watcher_binding_(this) {}
  ~DevUserShellApp() override = default;

 private:
  // |SingleServiceViewApp|
  void CreateView(
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      fidl::InterfaceRequest<modular::ServiceProvider> services) override {
    view_owner_request_ = std::move(view_owner_request);
    Connect();
  }

  // |UserShell|
  void Initialize(fidl::InterfaceHandle<modular::StoryProvider> story_provider,
                  fidl::InterfaceHandle<maxwell::SuggestionProvider>
                      suggestion_provider,
                  fidl::InterfaceRequest<modular::FocusController>
                  focus_controller_request) override {
    story_provider_.Bind(std::move(story_provider));
    Connect();
  }

  void Connect() {
    if (!view_owner_request_ || !story_provider_) {
      // Not yet ready, wait for the other of CreateView() and
      // Initialize() to be called.
      return;
    }

    FTL_LOG(INFO) << "DevUserShell START " << settings_.root_module
                  << " " << settings_.root_link;

    story_provider_->CreateStory(settings_.root_module,
                                 fidl::GetProxy(&story_controller_));

    fidl::InterfaceHandle<StoryWatcher> story_watcher;
    story_watcher_binding_.Bind(fidl::GetProxy(&story_watcher));
    story_controller_->Watch(std::move(story_watcher));

    story_controller_->Start(std::move(view_owner_request_));

    if (!settings_.root_link.empty()) {
      modular::LinkPtr root;
      story_controller_->GetLink(GetProxy(&root));

      rapidjson::Document document;
      document.Parse(settings_.root_link.c_str());
      root->SetAllDocuments(modular::ConvertToLink(document));
    }
  }

  // |StoryWatcher|
  void OnStart() override {}

  // |StoryWatcher|
  void OnData() override {}

  // |StoryWatcher|
  void OnStop() override {}

  // |StoryWatcher|
  void OnError() override {}

  // |StoryWatcher|
  void OnDone() override {
    FTL_LOG(INFO) << "DevUserShell DONE";
    story_controller_->Stop([this]() {
      FTL_LOG(INFO) << "DevUserShell STOP";
      story_watcher_binding_.Close();
      story_controller_.reset();
    });
  }

  fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request_;
  const Settings settings_;
  fidl::Binding<modular::StoryWatcher> story_watcher_binding_;
  modular::StoryProviderPtr story_provider_;
  modular::StoryControllerPtr story_controller_;

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
