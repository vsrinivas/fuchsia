// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/launcher/launcher_app.h"

#include "apps/fonts/services/font_provider.fidl.h"
#include "apps/modular/lib/app/connect.h"
#include "apps/mozart/services/views/view_provider.fidl.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/split_string.h"

namespace launcher {
namespace {
// TODO(jeffbrown): Don't hardcode this.
constexpr const char* kCompositorUrl = "file:///system/apps/compositor_service";
constexpr const char* kViewManagerUrl =
    "file:///system/apps/view_manager_service";
constexpr const char* kInputManagerUrl =
    "file:///system/apps/input_manager_service";
constexpr const char* kFontProviderUrl = "file:///system/apps/fonts";
}

LauncherApp::LauncherApp(const ftl::CommandLine& command_line)
    : application_context_(
          modular::ApplicationContext::CreateFromStartupInfo()),
      env_host_binding_(this) {
  FTL_DCHECK(application_context_);

  // Parse arguments.
  std::string option;
  command_line.GetOptionValue("view_associate_urls", &option);
  view_associate_urls_ = ftl::SplitStringCopy(option, ",", ftl::kKeepWhitespace,
                                              ftl::kSplitWantAll);
  if (view_associate_urls_.empty()) {
    // TODO(jeffbrown): Don't hardcode this.
    view_associate_urls_.push_back(kInputManagerUrl);
  }

  // Set up environment for the programs the launcher will run.
  modular::ApplicationEnvironmentHostPtr env_host;
  env_host_binding_.Bind(GetProxy(&env_host));
  application_context_->environment()->CreateNestedEnvironment(
      std::move(env_host), GetProxy(&env_), GetProxy(&env_controller_));
  env_->GetApplicationLauncher(GetProxy(&env_launcher_));
  RegisterServices();

  // Launch program with arguments supplied on command-line.
  const auto& positional_args = command_line.positional_args();
  if (!positional_args.empty()) {
    fidl::String url = positional_args[0];
    fidl::Array<fidl::String> arguments;
    for (size_t i = 1; i < positional_args.size(); ++i)
      arguments.push_back(positional_args[i]);
    Launch(std::move(url), std::move(arguments));
  }
}

LauncherApp::~LauncherApp() {}

void LauncherApp::GetApplicationEnvironmentServices(
    const fidl::String& url,
    fidl::InterfaceRequest<modular::ServiceProvider> environment_services) {
  env_services_.AddBinding(std::move(environment_services));
}

void LauncherApp::RegisterServices() {
  env_services_.AddService<mozart::Compositor>([this](
      fidl::InterfaceRequest<mozart::Compositor> request) {
    FTL_DLOG(INFO) << "Servicing compositor service request";
    InitCompositor();
    modular::ConnectToService(compositor_services_.get(), std::move(request));
  });

  env_services_.AddService<mozart::ViewManager>([this](
      fidl::InterfaceRequest<mozart::ViewManager> request) {
    FTL_DLOG(INFO) << "Servicing view manager service request";
    InitViewManager();
    modular::ConnectToService(view_manager_services_.get(), std::move(request));
  });

  env_services_.AddService<mozart::Launcher>(
      [this](fidl::InterfaceRequest<mozart::Launcher> request) {
        FTL_DLOG(INFO) << "Servicing launcher service request";
        launcher_bindings_.AddBinding(this, std::move(request));
      });

  env_services_.AddService<modular::ApplicationEnvironment>(
      [this](fidl::InterfaceRequest<modular::ApplicationEnvironment> request) {
        // TODO(jeffbrown): The fact we have to handle this here suggests that
        // the application protocol should change so as to pass the environment
        // as an initial rather than incoming services so we're not trying
        // to ask the incoming services for the environment.
        FTL_DLOG(INFO) << "Servicing application environment request";
        env_->Duplicate(std::move(request));
      });

  RegisterSingletonService(fonts::FontProvider::Name_, kFontProviderUrl);

  env_services_.SetDefaultServiceConnector([this](std::string service_name,
                                                  mx::channel channel) {
    FTL_DLOG(INFO) << "Servicing default service request for " << service_name;
    application_context_->environment_services()->ConnectToService(
        service_name, std::move(channel));
  });
}

void LauncherApp::RegisterSingletonService(std::string service_name,
                                           std::string url) {
  env_services_.AddServiceForName(
      ftl::MakeCopyable([
        this, service_name, url, services = modular::ServiceProviderPtr()
      ](mx::channel client_handle) mutable {
        FTL_DLOG(INFO) << "Servicing singleton service request for "
                       << service_name;
        if (!services) {
          auto launch_info = modular::ApplicationLaunchInfo::New();
          launch_info->url = url;
          launch_info->services = GetProxy(&services);
          env_launcher_->CreateApplication(std::move(launch_info), nullptr);
        }
        services->ConnectToService(service_name, std::move(client_handle));
      }),
      service_name);
}

void LauncherApp::InitCompositor() {
  if (compositor_)
    return;

  auto launch_info = modular::ApplicationLaunchInfo::New();
  launch_info->url = kCompositorUrl;
  launch_info->services = GetProxy(&compositor_services_);
  env_launcher_->CreateApplication(std::move(launch_info), nullptr);
  modular::ConnectToService(compositor_services_.get(), GetProxy(&compositor_));
  compositor_.set_connection_error_handler([] {
    FTL_LOG(ERROR) << "Exiting due to compositor connection error.";
    exit(1);
  });
}

void LauncherApp::InitViewManager() {
  if (view_manager_)
    return;

  auto launch_info = modular::ApplicationLaunchInfo::New();
  launch_info->url = kViewManagerUrl;
  launch_info->services = GetProxy(&view_manager_services_);
  env_launcher_->CreateApplication(std::move(launch_info), nullptr);
  modular::ConnectToService(view_manager_services_.get(),
                            GetProxy(&view_manager_));
  view_manager_.set_connection_error_handler([] {
    FTL_LOG(ERROR) << "Exiting due to view manager connection error.";
    exit(1);
  });

  // Launch view associated.
  for (const auto& url : view_associate_urls_) {
    FTL_DLOG(INFO) << "Starting view associate " << url;

    // Connect to the ViewAssociate.
    modular::ServiceProviderPtr view_associate_services;
    auto view_associate_launch_info = modular::ApplicationLaunchInfo::New();
    view_associate_launch_info->url = url;
    view_associate_launch_info->services = GetProxy(&view_associate_services);
    env_launcher_->CreateApplication(std::move(view_associate_launch_info),
                                     nullptr);
    auto view_associate = modular::ConnectToService<mozart::ViewAssociate>(
        view_associate_services.get());

    // Wire up the associate to the ViewManager.
    mozart::ViewAssociateOwnerPtr view_associate_owner;
    view_manager_->RegisterViewAssociate(std::move(view_associate),
                                         GetProxy(&view_associate_owner), url);
    view_associate_owner.set_connection_error_handler([url] {
      FTL_LOG(ERROR) << "Exiting due to view associate connection error: url="
                     << url;
      exit(1);
    });
    view_associate_owners_.push_back(std::move(view_associate_owner));
  }
  view_manager_->FinishedRegisteringViewAssociates();
}

void LauncherApp::Launch(fidl::String url,
                         fidl::Array<fidl::String> arguments) {
  FTL_LOG(INFO) << "Launching " << url;

  modular::ServiceProviderPtr services;
  modular::ApplicationControllerPtr controller;
  auto launch_info = modular::ApplicationLaunchInfo::New();
  launch_info->url = std::move(url);
  launch_info->arguments = std::move(arguments);
  launch_info->services = GetProxy(&services);
  env_launcher_->CreateApplication(std::move(launch_info),
                                   GetProxy(&controller));

  fidl::InterfacePtr<mozart::ViewProvider> view_provider;
  modular::ConnectToService(services.get(), GetProxy(&view_provider));

  fidl::InterfaceHandle<mozart::ViewOwner> view_owner;
  view_provider->CreateView(fidl::GetProxy(&view_owner), nullptr);

  DisplayInternal(std::move(view_owner), std::move(controller));
}

void LauncherApp::Display(
    fidl::InterfaceHandle<mozart::ViewOwner> view_owner_handle) {
  DisplayInternal(std::move(view_owner_handle), nullptr);
}

void LauncherApp::DisplayInternal(
    fidl::InterfaceHandle<mozart::ViewOwner> view_owner_handle,
    modular::ApplicationControllerPtr controller) {
  mozart::ViewOwnerPtr view_owner =
      mozart::ViewOwnerPtr::Create(std::move(view_owner_handle));

  InitCompositor();
  InitViewManager();

  const uint32_t next_id = next_id_++;
  std::unique_ptr<LaunchInstance> instance(
      new LaunchInstance(compositor_.get(), view_manager_.get(),
                         std::move(view_owner), std::move(controller),
                         [this, next_id] { OnLaunchTermination(next_id); }));
  instance->Launch();
  launch_instances_.emplace(next_id, std::move(instance));
}

void LauncherApp::OnLaunchTermination(uint32_t id) {
  launch_instances_.erase(id);

  if (launch_instances_.empty()) {
    FTL_LOG(INFO) << "Last launched view terminated, exiting launcher.";
    exit(0);
  }
}

}  // namespace launcher
