// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/launcher/launcher_app.h"

#include "apps/mozart/glue/base/logging.h"
#include "apps/mozart/glue/base/trace_event.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/strings/split_string.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"

namespace launcher {

LauncherApp::LauncherApp() : next_id_(0u) {}

LauncherApp::~LauncherApp() {}

void LauncherApp::OnInitialize() {
  auto command_line = ftl::CommandLineFromIteratorsWithArgv0(
      url(), args().begin(), args().end());

  TRACE_EVENT0("launcher", __func__);

  InitCompositor();
  InitViewManager();
  std::string view_associate_urls;
  command_line.GetOptionValue("view_associate_urls", &view_associate_urls);
  // If view_associate_urls is empty, a default set of ViewAssociates is
  // launched
  InitViewAssociates(view_associate_urls);

  for (const auto& arg : command_line.positional_args()) {
    Launch(arg);
  }
}

void LauncherApp::InitCompositor() {
  mojo::ConnectToService(shell(), "mojo:compositor_service",
                         GetProxy(&compositor_));
  compositor_.set_connection_error_handler(
      [this] { OnCompositorConnectionError(); });
}

void LauncherApp::InitViewManager() {
  mojo::ConnectToService(shell(), "mojo:view_manager_service",
                         GetProxy(&view_manager_));
  view_manager_.set_connection_error_handler(
      [this] { OnViewManagerConnectionError(); });
}

void LauncherApp::InitViewAssociates(
    const std::string& associate_urls_command_line_param) {
  // Build up the list of ViewAssociates we are going to start
  auto associate_urls =
      ftl::SplitStringCopy(associate_urls_command_line_param, ",",
                           ftl::kKeepWhitespace, ftl::kSplitWantAll);

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
    mozart::ViewAssociatePtr view_associate;
    mojo::ConnectToService(shell(), url, GetProxy(&view_associate));

    // Wire up the associate to the ViewManager.
    mozart::ViewAssociateOwnerPtr view_associate_owner;
    view_manager_->RegisterViewAssociate(std::move(view_associate),
                                         GetProxy(&view_associate_owner), url);

    view_associate_owner.set_connection_error_handler(
        [this] { OnViewAssociateConnectionError(); });

    view_associate_owners_.push_back(std::move(view_associate_owner));
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
          bindings_.AddBinding(this, std::move(launcher_request));
        });
  }
  return true;
}

void LauncherApp::Launch(const mojo::String& application_url) {
  DVLOG(1) << "Launching " << application_url;

  mojo::ConnectToService(shell(), "mojo:framebuffer",
                         mojo::GetProxy(&framebuffer_provider_));
  framebuffer_provider_->Create([this, application_url](
      mojo::InterfaceHandle<mojo::Framebuffer> framebuffer,
      mojo::FramebufferInfoPtr framebuffer_info) {
    FTL_CHECK(framebuffer);
    FTL_CHECK(framebuffer_info);

    mozart::ViewProviderPtr view_provider;
    mojo::ConnectToService(shell(), application_url, GetProxy(&view_provider));

    LaunchInternal(std::move(framebuffer), std::move(framebuffer_info),
                   std::move(view_provider));
  });
}

void LauncherApp::LaunchInternal(
    mojo::InterfaceHandle<mojo::Framebuffer> framebuffer,
    mojo::FramebufferInfoPtr framebuffer_info,
    mozart::ViewProviderPtr view_provider) {
  const uint32_t next_id = next_id_++;
  std::unique_ptr<LaunchInstance> instance(new LaunchInstance(
      compositor_.get(), view_manager_.get(), std::move(framebuffer),
      std::move(framebuffer_info), std::move(view_provider),
      [this, next_id] { OnLaunchTermination(next_id); }));
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
  FTL_LOG(ERROR) << "Exiting due to compositor connection error.";
  mojo::TerminateApplication(MOJO_RESULT_UNKNOWN);
}

void LauncherApp::OnViewManagerConnectionError() {
  FTL_LOG(ERROR) << "Exiting due to view manager connection error.";
  mojo::TerminateApplication(MOJO_RESULT_UNKNOWN);
}

void LauncherApp::OnViewAssociateConnectionError() {
  FTL_LOG(ERROR) << "Exiting due to view associate connection error.";
  mojo::TerminateApplication(MOJO_RESULT_UNKNOWN);
};

}  // namespace launcher
