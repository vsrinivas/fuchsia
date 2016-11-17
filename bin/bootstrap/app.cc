// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/bootstrap/app.h"

#include "apps/modular/lib/app/connect.h"
#include "apps/modular/src/bootstrap/params.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"

namespace bootstrap {
namespace {

// TODO(jeffbrown): Remove special stuff for view manager somehow.
constexpr const char kViewManagerUrl[] =
    "file:///system/apps/view_manager_service";
constexpr const char* kViewManagerAssociates[] = {
    "file:///system/apps/input_manager_service",
};

}  // namespace

App::App(Params* params)
    : application_context_(
          modular::ApplicationContext::CreateFromStartupInfo()),
      env_host_binding_(this) {
  FTL_DCHECK(application_context_);

  // Set up environment for the programs we will run.
  modular::ApplicationEnvironmentHostPtr env_host;
  env_host_binding_.Bind(GetProxy(&env_host));
  application_context_->environment()->CreateNestedEnvironment(
      std::move(env_host), GetProxy(&env_), GetProxy(&env_controller_),
      params->label());
  env_->GetApplicationLauncher(GetProxy(&env_launcher_));

  // Register services.
  for (auto& pair : params->TakeServices())
    RegisterSingleton(pair.first, std::move(pair.second));
  RegisterDefaultServiceConnector();

  // TODO(jeffbrown): Remove this.
  RegisterViewManager();

  // Launch application specified on command-line.
  LaunchApplication(params->TakeInitialLaunch());
}

App::~App() {}

void App::RegisterSingleton(std::string service_name,
                            modular::ApplicationLaunchInfoPtr launch_info) {
  env_services_.AddServiceForName(
      ftl::MakeCopyable([
        this, service_name, launch_info = std::move(launch_info),
        controller = modular::ApplicationControllerPtr()
      ](mx::channel client_handle) mutable {
        FTL_VLOG(2) << "Servicing singleton service request for "
                    << service_name;
        auto it = service_providers_.find(launch_info->url);
        if (it == service_providers_.end()) {
          FTL_VLOG(1) << "Starting singleton " << launch_info->url
                      << " for service " << service_name;
          modular::ServiceProviderPtr service_provider;
          auto dup_launch_info = modular::ApplicationLaunchInfo::New();
          dup_launch_info->url = launch_info->url;
          dup_launch_info->arguments = launch_info->arguments.Clone();
          dup_launch_info->services = GetProxy(&service_provider);
          env_launcher_->CreateApplication(std::move(dup_launch_info),
                                           GetProxy(&controller));
          service_provider.set_connection_error_handler(
              [ this, url = launch_info->url, &controller ] {
                FTL_LOG(ERROR) << "Singleton " << url << " died";
                controller.reset();  // kills the singleton application
                service_providers_.erase(url);
              });

          std::tie(it, std::ignore) = service_providers_.emplace(
              launch_info->url, std::move(service_provider));
        }

        it->second->ConnectToService(service_name, std::move(client_handle));
      }),
      service_name);
}

void App::RegisterDefaultServiceConnector() {
  env_services_.SetDefaultServiceConnector(
      [this](std::string service_name, mx::channel channel) {
        FTL_VLOG(2) << "Servicing default service request for " << service_name;
        application_context_->environment_services()->ConnectToService(
            service_name, std::move(channel));
      });
}

void App::RegisterViewManager() {
  env_services_.AddService<mozart::ViewManager>([this](
      fidl::InterfaceRequest<mozart::ViewManager> request) {
    FTL_VLOG(2) << "Servicing view manager service request";
    InitViewManager();
    modular::ConnectToService(view_manager_services_.get(), std::move(request));
  });
}

void App::InitViewManager() {
  if (view_manager_)
    return;

  FTL_VLOG(1) << "Starting view manager";
  auto launch_info = modular::ApplicationLaunchInfo::New();
  launch_info->url = kViewManagerUrl;
  launch_info->services = GetProxy(&view_manager_services_);
  env_launcher_->CreateApplication(std::move(launch_info),
                                   GetProxy(&view_manager_controller_));
  modular::ConnectToService(view_manager_services_.get(),
                            GetProxy(&view_manager_));
  view_manager_.set_connection_error_handler([this] {
    FTL_LOG(ERROR) << "View manager died";
    ResetViewManager();
  });

  // Launch view associates.
  for (const auto& url : kViewManagerAssociates) {
    FTL_VLOG(1) << "Starting view associate " << url;
    modular::ServiceProviderPtr services;
    modular::ApplicationControllerPtr controller;
    auto launch_info = modular::ApplicationLaunchInfo::New();
    launch_info->url = url;
    launch_info->services = GetProxy(&services);
    env_launcher_->CreateApplication(std::move(launch_info),
                                     GetProxy(&controller));
    auto view_associate =
        modular::ConnectToService<mozart::ViewAssociate>(services.get());

    // Wire up the associate to the ViewManager.
    mozart::ViewAssociateOwnerPtr owner;
    view_manager_->RegisterViewAssociate(std::move(view_associate),
                                         GetProxy(&owner), url);
    owner.set_connection_error_handler([this, url] {
      FTL_LOG(ERROR) << "View associate " << url << " died";
      ResetViewManager();
    });
    view_associate_controllers_.push_back(std::move(controller));
    view_associate_owners_.push_back(std::move(owner));
  }
  view_manager_->FinishedRegisteringViewAssociates();
}

void App::ResetViewManager() {
  view_manager_controller_.reset();     // kills the view manager application
  view_associate_controllers_.clear();  // kills the view associates
  view_manager_.reset();
  view_manager_services_.reset();
  view_associate_owners_.clear();
}

void App::LaunchApplication(modular::ApplicationLaunchInfoPtr launch_info) {
  FTL_LOG(INFO) << "Bootstrapping application " << launch_info->url;

  modular::ApplicationControllerPtr controller;
  env_launcher_->CreateApplication(std::move(launch_info),
                                   GetProxy(&controller));
  controller.set_connection_error_handler([] {
    FTL_LOG(INFO) << "Bootstrapped application terminated.";
    exit(0);
  });
}

void App::GetApplicationEnvironmentServices(
    fidl::InterfaceRequest<modular::ServiceProvider> environment_services) {
  env_services_.AddBinding(std::move(environment_services));
}

}  // namespace bootstrap
