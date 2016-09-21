// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/ui/launcher/launcher_app.h"

#include "base/command_line.h"
#include "base/logging.h"
#include "base/strings/string_split.h"
#include "base/trace_event/trace_event.h"
#include "mojo/common/tracing_impl.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"

namespace launcher {

LauncherApp::LauncherApp() : next_id_(0u) {}

LauncherApp::~LauncherApp() {}

void LauncherApp::OnInitialize() {
  auto command_line = base::CommandLine::ForCurrentProcess();
  command_line->InitFromArgv(args());
  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_SYSTEM_DEBUG_LOG;
  logging::InitLogging(settings);

  tracing_.Initialize(shell(), &args());
  TRACE_EVENT0("launcher", __func__);

  InitCompositor();
  InitViewManager();
  InitViewAssociates(command_line->GetSwitchValueASCII("view_associate_urls"));

  for (size_t i = 0; i < command_line->GetArgs().size(); ++i) {
    Launch(command_line->GetArgs()[i]);
  }
}

void LauncherApp::InitCompositor() {
  mojo::ConnectToService(shell(), "mojo:compositor_service",
                         GetProxy(&compositor_));
  compositor_.set_connection_error_handler(base::Bind(
      &LauncherApp::OnCompositorConnectionError, base::Unretained(this)));
}

void LauncherApp::InitViewManager() {
  mojo::ConnectToService(shell(), "mojo:view_manager_service",
                         GetProxy(&view_manager_));
  view_manager_.set_connection_error_handler(base::Bind(
      &LauncherApp::OnViewManagerConnectionError, base::Unretained(this)));
}

void LauncherApp::InitViewAssociates(
    const std::string& associate_urls_command_line_param) {
  // Build up the list of ViewAssociates we are going to start
  auto associate_urls =
      SplitString(associate_urls_command_line_param, ",", base::KEEP_WHITESPACE,
                  base::SPLIT_WANT_ALL);

  // If there's nothing we got from the command line, use our own list
  if (associate_urls.empty()) {
    // TODO(jeffbrown): Replace this hardcoded list.
    associate_urls.push_back("mojo:input_manager_service");
  }

  view_associate_owners_.reserve(associate_urls.size());

  // Connect to ViewAssociates.
  for (const auto& url : associate_urls) {
    // Connect to the ViewAssociate.
    DVLOG(2) << "Connecting to ViewAssociate " << url;
    mojo::ui::ViewAssociatePtr view_associate;
    mojo::ConnectToService(shell(), url, GetProxy(&view_associate));

    // Wire up the associate to the ViewManager.
    mojo::ui::ViewAssociateOwnerPtr view_associate_owner;
    view_manager_->RegisterViewAssociate(view_associate.Pass(),
                                         GetProxy(&view_associate_owner), url);

    view_associate_owner.set_connection_error_handler(base::Bind(
        &LauncherApp::OnViewAssociateConnectionError, base::Unretained(this)));

    view_associate_owners_.push_back(view_associate_owner.Pass());
  }
  view_manager_->FinishedRegisteringViewAssociates();
}

bool LauncherApp::OnAcceptConnection(
    mojo::ServiceProviderImpl* service_provider_impl) {
  // Only present the launcher interface to the shell.
  if (service_provider_impl->connection_context().remote_url.empty()) {
    service_provider_impl->AddService<Launcher>(
        [this](const mojo::ConnectionContext& connection_context,
               mojo::InterfaceRequest<Launcher> launcher_request) {
          bindings_.AddBinding(this, launcher_request.Pass());
        });
  }
  return true;
}

void LauncherApp::Launch(const mojo::String& application_url) {
  DVLOG(1) << "Launching " << application_url;

  mojo::NativeViewportPtr viewport;
  mojo::ConnectToService(shell(), "mojo:native_viewport_service",
                         GetProxy(&viewport));

  mojo::ui::ViewProviderPtr view_provider;
  mojo::ConnectToService(shell(), application_url, GetProxy(&view_provider));

  LaunchInternal(viewport.Pass(), view_provider.Pass());
}

void LauncherApp::LaunchOnViewport(
    mojo::InterfaceHandle<mojo::NativeViewport> viewport,
    mojo::InterfaceHandle<mojo::ui::ViewProvider> view_provider) {
  LaunchInternal(mojo::NativeViewportPtr::Create(viewport.Pass()),
                 mojo::ui::ViewProviderPtr::Create(view_provider.Pass()));
}

void LauncherApp::LaunchInternal(mojo::NativeViewportPtr viewport,
                                 mojo::ui::ViewProviderPtr view_provider) {
  uint32_t next_id = next_id_++;
  std::unique_ptr<LaunchInstance> instance(new LaunchInstance(
      viewport.Pass(), view_provider.Pass(), compositor_.get(),
      view_manager_.get(), base::Bind(&LauncherApp::OnLaunchTermination,
                                      base::Unretained(this), next_id)));
  instance->Launch();
  launch_instances_.emplace(next_id, std::move(instance));
}

void LauncherApp::OnLaunchTermination(uint32_t id) {
  launch_instances_.erase(id);
  if (launch_instances_.empty()) {
    mojo::TerminateApplication(MOJO_RESULT_OK);
  }
}

void LauncherApp::OnCompositorConnectionError() {
  LOG(ERROR) << "Exiting due to compositor connection error.";
  mojo::TerminateApplication(MOJO_RESULT_UNKNOWN);
}

void LauncherApp::OnViewManagerConnectionError() {
  LOG(ERROR) << "Exiting due to view manager connection error.";
  mojo::TerminateApplication(MOJO_RESULT_UNKNOWN);
}

void LauncherApp::OnViewAssociateConnectionError() {
  LOG(ERROR) << "Exiting due to view associate connection error.";
  mojo::TerminateApplication(MOJO_RESULT_UNKNOWN);
};

}  // namespace launcher
