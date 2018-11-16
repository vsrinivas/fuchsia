// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/sysmgr/app.h"

#include <zircon/process.h>
#include <zircon/processargs.h>

#include <fs/managed-vfs.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/fdio/util.h>
#include "lib/component/cpp/connect.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"

namespace sysmgr {

namespace {
constexpr char kDefaultLabel[] = "sys";
#ifdef AUTO_UPDATE_PACKAGES
constexpr bool kAutoUpdatePackages = true;
#else
constexpr bool kAutoUpdatePackages = false;
#endif
}  // namespace

App::App(Config config)
    : startup_context_(component::StartupContext::CreateFromStartupInfo()),
      vfs_(async_get_default_dispatcher()),
      svc_root_(fbl::AdoptRef(new fs::PseudoDir())) {
  FXL_DCHECK(startup_context_);

  // The set of excluded services below are services that are the transitive
  // closure of dependencies required for auto-updates that must not be resolved
  // via the update service.
  const auto update_dependencies = config.TakeUpdateDependencies();
  std::unordered_set<std::string> update_dependency_urls;

  // Register services.
  for (auto& pair : config.TakeServices()) {
    if (std::find(update_dependencies.begin(), update_dependencies.end(),
                  pair.first) != std::end(update_dependencies)) {
      update_dependency_urls.insert(*pair.second->url);
    }
    RegisterSingleton(pair.first, std::move(pair.second));
  }

  // Ordering note: The impl of CreateNestedEnvironment will resolve the
  // delegating app loader. However, since its call back to the host directory
  // won't happen until the next (first) message loop iteration, we'll be set up
  // by then.
  auto env_request = env_.NewRequest();
  env_->GetLauncher(env_launcher_.NewRequest());
  env_->GetServices(env_services_.NewRequest());

  // Register the app loaders. First initialize and pass the amber client if
  // auto_update_packages is enabled. Note that we have to do this after
  // |env_services_| is initialized.
  fuchsia::amber::ControlPtr amber_ctl;
  if (kAutoUpdatePackages) {
    static const char* const kAmberName = fuchsia::amber::Control::Name_;
    bool amber_missing =
        std::find(update_dependencies.begin(), update_dependencies.end(),
                  kAmberName) == update_dependencies.end();
    // Check if any component urls that are excluded (dependencies of
    // Amber/startup) were not registered from the above configuration.
    bool missing_services = false;
    for (auto& dep : update_dependencies) {
      if (std::find(svc_names_->begin(), svc_names_->end(), dep) ==
          svc_names_->end()) {
        FXL_LOG(WARNING) << "missing service required for auto updates: " << dep;
        missing_services = true;
      }
    }
    if (amber_missing || missing_services) {
      FXL_LOG(WARNING) << "auto_update_packages = true but some update "
                          "dependencies are missing in the sys environment. "
                          "Disabling auto-updates.";
    } else {
      env_services_->ConnectToService(kAmberName,
                                      amber_ctl.NewRequest().TakeChannel());
    }
  }
  RegisterAppLoaders(config.TakeAppLoaders(), std::move(update_dependency_urls),
                     std::move(amber_ctl));

  // Set up environment for the programs we will run.
  fuchsia::sys::ServiceListPtr service_list(new fuchsia::sys::ServiceList);
  service_list->names = std::move(svc_names_);
  service_list->host_directory = OpenAsDirectory();
  startup_context_->environment()->CreateNestedEnvironment(
      std::move(env_request), env_controller_.NewRequest(), kDefaultLabel,
      std::move(service_list), {});

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
  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) != ZX_OK)
    return zx::channel();
  if (vfs_.ServeDirectory(svc_root_, std::move(h1)) != ZX_OK)
    return zx::channel();
  return h2;
}

void App::ConnectToService(const std::string& service_name,
                           zx::channel channel) {
  fbl::RefPtr<fs::Vnode> child;
  svc_root_->Lookup(&child, service_name);
  auto status = child->Serve(&vfs_, std::move(channel), 0);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Could not serve " << service_name << ": " << status;
  }
}

void App::RegisterSingleton(std::string service_name,
                            fuchsia::sys::LaunchInfoPtr launch_info) {
  auto child = fbl::AdoptRef(
      new fs::Service([this, service_name, launch_info = std::move(launch_info),
                       controller = fuchsia::sys::ComponentControllerPtr()](
                          zx::channel client_handle) mutable {
        FXL_VLOG(2) << "Servicing singleton service request for "
                    << service_name;
        auto it = services_.find(launch_info->url);
        if (it == services_.end()) {
          FXL_VLOG(1) << "Starting singleton " << launch_info->url
                      << " for service " << service_name;
          component::Services services;
          fuchsia::sys::LaunchInfo dup_launch_info;
          dup_launch_info.url = launch_info->url;
          fidl::Clone(launch_info->arguments, &dup_launch_info.arguments);
          dup_launch_info.directory_request = services.NewRequest();
          env_launcher_->CreateComponent(std::move(dup_launch_info),
                                         controller.NewRequest());
          controller.set_error_handler(
              [this, url = launch_info->url, &controller](zx_status_t error) {
                FXL_LOG(ERROR) << "Singleton " << url << " died";
                controller.Unbind();  // kills the singleton application
                services_.erase(url);
              });

          std::tie(it, std::ignore) =
              services_.emplace(launch_info->url, std::move(services));
        }

        it->second.ConnectToService(std::move(client_handle), service_name);
        return ZX_OK;
      }));
  svc_names_.push_back(service_name);
  svc_root_->AddEntry(service_name, std::move(child));
}

void App::RegisterAppLoaders(Config::ServiceMap app_loaders,
                             std::unordered_set<std::string> update_dependency_urls,
                             fuchsia::amber::ControlPtr amber_ctl) {
  if (amber_ctl) {
    app_loader_ = DelegatingLoader::MakeWithPackageUpdatingFallback(
        std::move(app_loaders), env_launcher_.get(), std::move(update_dependency_urls),
        std::move(amber_ctl));
  } else {
    app_loader_ = DelegatingLoader::MakeWithParentFallback(
        std::move(app_loaders), env_launcher_.get(),
        startup_context_->ConnectToEnvironmentService<fuchsia::sys::Loader>());
  }
  auto child = fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
    app_loader_bindings_.AddBinding(
        app_loader_.get(),
        fidl::InterfaceRequest<fuchsia::sys::Loader>(std::move(channel)));
    return ZX_OK;
  }));
  static const char* kLoaderName = fuchsia::sys::Loader::Name_;
  svc_names_.push_back(kLoaderName);
  svc_root_->AddEntry(kLoaderName, std::move(child));
}

void App::LaunchApplication(fuchsia::sys::LaunchInfo launch_info) {
  FXL_VLOG(1) << "Launching application " << launch_info.url;
  env_launcher_->CreateComponent(std::move(launch_info), nullptr);
}

}  // namespace sysmgr
