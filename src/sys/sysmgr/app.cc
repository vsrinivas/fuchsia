// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/sysmgr/app.h"

#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/default.h>
#include <lib/async/dispatcher.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/vfs/cpp/service.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include "src/lib/fxl/logging.h"

namespace sysmgr {

namespace {
constexpr char kDefaultLabel[] = "sys";
#ifdef AUTO_UPDATE_PACKAGES
constexpr bool kAutoUpdatePackages = true;
#else
constexpr bool kAutoUpdatePackages = false;
#endif
}  // namespace

App::App(Config config, async::Loop* loop)
    : component_context_(sys::ComponentContext::Create()),
      auto_updates_enabled_(kAutoUpdatePackages) {
  FXL_DCHECK(component_context_);

  // The set of excluded services below are services that are the transitive
  // closure of dependencies required for auto-updates that must not be resolved
  // via the update service.
  const auto update_dependencies = config.TakeUpdateDependencies();
  const auto optional_services = config.TakeOptionalServices();
  std::unordered_set<std::string> update_dependency_urls;

  // prepare this realm's diagnostics platform's service directory
  fuchsia::sys::LaunchInfo launch_diagnostics;
  launch_diagnostics.url = config.diagnostics_url();
  if (!launch_diagnostics.url.empty()) {
    auto diagnostics_services =
        sys::ServiceDirectory::CreateWithRequest(&launch_diagnostics.directory_request);
    services_.emplace(launch_diagnostics.url, std::move(diagnostics_services));
  } else {
    FXL_LOG(WARNING) << "Creating sys realm without a diagnostics service.";
  }

  // Register services.
  for (auto& pair : config.TakeServices()) {
    if (std::find(update_dependencies.begin(), update_dependencies.end(), pair.first) !=
        std::end(update_dependencies)) {
      update_dependency_urls.insert(pair.second->url);
    }
    const bool optional = std::find(optional_services.begin(), optional_services.end(),
                                    pair.first) != std::end(optional_services);
    RegisterSingleton(pair.first, std::move(pair.second), optional);
  }

  auto env_request = env_.NewRequest();
  fuchsia::sys::ServiceProviderPtr env_services;
  env_->GetLauncher(env_launcher_.NewRequest());
  env_->GetServices(env_services.NewRequest());

  if (auto_updates_enabled_) {
    const bool resolver_missing =
        std::find(update_dependencies.begin(), update_dependencies.end(),
                  fuchsia::pkg::PackageResolver::Name_) == update_dependencies.end();
    // Check if any component urls that are excluded (dependencies of
    // PackageResolver/startup) were not registered from the above
    // configuration.
    bool missing_services = false;
    for (auto& dep : update_dependencies) {
      if (std::find(svc_names_.begin(), svc_names_.end(), dep) == svc_names_.end()) {
        FXL_LOG(WARNING) << "missing service required for auto updates: " << dep;
        missing_services = true;
      }
    }

    if (resolver_missing || missing_services) {
      FXL_LOG(WARNING) << "auto_update_packages = true but some update "
                          "dependencies are missing in the sys environment. "
                          "Disabling auto-updates.";
      auto_updates_enabled_ = false;
    }
  }

  // Configure loader.
  if (auto_updates_enabled_) {
    package_updating_loader_ = std::make_unique<PackageUpdatingLoader>(
        std::move(update_dependency_urls), std::move(env_services), async_get_default_dispatcher());
  }
  static const char* const kLoaderName = fuchsia::sys::Loader::Name_;
  auto child =
      std::make_unique<vfs::Service>([this](zx::channel channel, async_dispatcher_t* dispatcher) {
        if (auto_updates_enabled_) {
          package_updating_loader_->Bind(
              fidl::InterfaceRequest<fuchsia::sys::Loader>(std::move(channel)));
        } else {
          component_context_->svc()->Connect(kLoaderName, std::move(channel));
        }
      });
  svc_names_.push_back(kLoaderName);
  svc_root_.AddEntry(kLoaderName, std::move(child));

  // Set up environment for the programs we will run.
  fuchsia::sys::ServiceListPtr service_list(new fuchsia::sys::ServiceList);
  service_list->names = std::move(svc_names_);
  service_list->host_directory = OpenAsDirectory();
  fuchsia::sys::EnvironmentPtr environment;
  component_context_->svc()->Connect(environment.NewRequest());
  // Inherit services from the root appmgr realm, which includes certain
  // services currently implemented by non-component processes that are passed
  // through appmgr to this sys realm. Note that |service_list| will override
  // the inherited services if it includes services also in the root realm.
  fuchsia::sys::EnvironmentOptions options = {.inherit_parent_services = true};
  environment->CreateNestedEnvironment(std::move(env_request), env_controller_.NewRequest(),
                                       kDefaultLabel, std::move(service_list), std::move(options));

  // before we start any configured services, make sure we start the Archivist. its sandbox includes
  // fuchsia.sys.LogConnector, so appmgr will create the correct LogConsumer buffer before this ends
  if (!launch_diagnostics.url.empty()) {
    StartDiagnostics(std::move(launch_diagnostics), loop);
  }

  // Connect to startup services
  for (auto& startup_service : config.TakeStartupServices()) {
    FXL_VLOG(1) << "Connecting to startup service " << startup_service;
    zx::channel h1, h2;
    zx::channel::create(0, &h1, &h2);
    ConnectToService(startup_service, std::move(h1));
  }

  // Launch startup applications.
  for (auto& launch_info : config.TakeApps()) {
    LaunchApplication(std::move(*launch_info));
  }
}

App::~App() = default;

zx::channel App::OpenAsDirectory() {
  fidl::InterfaceHandle<fuchsia::io::Directory> dir;
  svc_root_.Serve(fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
                  dir.NewRequest().TakeChannel());
  return dir.TakeChannel();
}

void App::ConnectToService(const std::string& service_name, zx::channel channel) {
  vfs::internal::Node* child;
  auto status = svc_root_.Lookup(service_name, &child);
  if (status == ZX_OK) {
    status = child->Serve(fuchsia::io::OPEN_RIGHT_READABLE, std::move(channel));
  }
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Could not serve " << service_name << ": " << status;
  }
}

void App::RegisterSingleton(std::string service_name, fuchsia::sys::LaunchInfoPtr launch_info,
                            bool optional) {
  auto child = std::make_unique<vfs::Service>([this, optional, service_name,
                                               launch_info = std::move(launch_info),
                                               controller = fuchsia::sys::ComponentControllerPtr()](
                                                  zx::channel client_handle,
                                                  async_dispatcher_t* dispatcher) mutable {
    FXL_VLOG(2) << "Servicing singleton service request for " << service_name;
    auto it = services_.find(launch_info->url);
    if (it == services_.end()) {
      FXL_VLOG(1) << "Starting singleton " << launch_info->url << " for service " << service_name;
      fuchsia::sys::LaunchInfo dup_launch_info;
      dup_launch_info.url = launch_info->url;
      fidl::Clone(launch_info->arguments, &dup_launch_info.arguments);
      auto services = sys::ServiceDirectory::CreateWithRequest(&dup_launch_info.directory_request);
      controller.events().OnTerminated = [service_name, url = launch_info->url, optional](
                                             int64_t return_code,
                                             fuchsia::sys::TerminationReason reason) {
        if (!optional && reason == fuchsia::sys::TerminationReason::PACKAGE_NOT_FOUND) {
          FXL_LOG(ERROR) << "Could not load package for service " << service_name << " at " << url;
        }
      };
      controller.set_error_handler(
          [this, optional, url = launch_info->url, &controller](zx_status_t error) {
            if (!optional) {
              FXL_LOG(ERROR) << "Singleton " << url << " died";
            }
            controller.Unbind();  // kills the singleton application
            services_.erase(url);
          });
      env_launcher_->CreateComponent(std::move(dup_launch_info), controller.NewRequest());

      std::tie(it, std::ignore) = services_.emplace(launch_info->url, std::move(services));
    }

    it->second->Connect(service_name, std::move(client_handle));
  });
  svc_names_.push_back(service_name);
  svc_root_.AddEntry(service_name, std::move(child));
}

void App::LaunchApplication(fuchsia::sys::LaunchInfo launch_info) {
  FXL_VLOG(1) << "Launching application " << launch_info.url;
  env_launcher_->CreateComponent(std::move(launch_info), nullptr);
}

void App::StartDiagnostics(fuchsia::sys::LaunchInfo launch_diagnostics, async::Loop* loop) {
  FXL_VLOG(1) << "Launching diagnostics from " << launch_diagnostics.url;
  bool diagnostics_ready = false;
  env_launcher_->CreateComponent(std::move(launch_diagnostics),
                                 diagnostics_controller_.NewRequest());
  diagnostics_controller_.events().OnDirectoryReady = [&diagnostics_ready] {
    diagnostics_ready = true;
  };
  diagnostics_controller_.set_error_handler(
      [this, url = launch_diagnostics.url](zx_status_t error) {
        FXL_LOG(ERROR) << "Singleton " << url << " died (diagnostics)";
        diagnostics_controller_.Unbind();
        services_.erase(url);
      });

  FXL_LOG(INFO) << "Waiting for diagnostics service dir";
  while (!diagnostics_ready) {
    // we're willing to wait an infinite time for a *single* turn of the loop, which we'll repeat
    loop->Run(zx::time::infinite(), true /*once*/);
  }
  FXL_LOG(INFO) << "Diagnostics initialized.";
}

}  // namespace sysmgr
