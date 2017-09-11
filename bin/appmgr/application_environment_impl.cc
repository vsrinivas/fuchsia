// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/application_environment_impl.h"

#include <fcntl.h>
#include <launchpad/launchpad.h>
#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/status.h>
#include <mx/process.h>
#include <mxio/namespace.h>
#include <mxio/util.h>
#include <unistd.h>

#include <utility>

#include "lib/app/cpp/connect.h"
#include "garnet/lib/far/format.h"
#include "garnet/bin/appmgr/namespace_builder.h"
#include "garnet/bin/appmgr/runtime_metadata.h"
#include "garnet/bin/appmgr/url_resolver.h"
#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/mtl/handles/object_info.h"

namespace app {
namespace {

constexpr mx_rights_t kChildJobRights =
    MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_WRITE;

constexpr char kFuchsiaMagic[] = "#!fuchsia ";
constexpr size_t kFuchsiaMagicLength = sizeof(kFuchsiaMagic) - 1;
constexpr size_t kMaxShebangLength = 2048;
constexpr char kNumberedLabelFormat[] = "env-%d";
constexpr char kAppPath[] = "bin/app";
constexpr char kRuntimePath[] = "meta/runtime";
constexpr char kSandboxPath[] = "meta/sandbox";

enum class LaunchType {
  kProcess,
  kArchive,
  kRunner,
};

std::vector<const char*> GetArgv(const ApplicationLaunchInfoPtr& launch_info) {
  std::vector<const char*> argv;
  argv.reserve(launch_info->arguments.size() + 1);
  argv.push_back(launch_info->url.get().c_str());
  for (const auto& argument : launch_info->arguments)
    argv.push_back(argument.get().c_str());
  return argv;
}

mx::channel TakeAppServices(ApplicationLaunchInfoPtr& launch_info) {
  if (launch_info->services)
    return launch_info->services.PassChannel();
  return mx::channel();
}

// The very first nested environment process we create gets the
// PA_SERVICE_REQUEST given to us by our parent. It's slightly awkward that we
// don't publish the root environment's services. We should consider
// reorganizing the boot process so that the root environment's services are
// the ones we want to publish.
void PublishServicesForFirstNestedEnvironment(ServiceProviderBridge* services) {
  static mx_handle_t request = mx_get_startup_handle(PA_SERVICE_REQUEST);
  if (request == MX_HANDLE_INVALID)
    return;
  services->ServeDirectory(mx::channel(request));
  request = MX_HANDLE_INVALID;
}

std::string GetLabelFromURL(const std::string& url) {
  size_t last_slash = url.rfind('/');
  if (last_slash == std::string::npos || last_slash + 1 == url.length())
    return url;
  return url.substr(last_slash + 1);
}

mx::process Launch(const mx::job& job,
                   const std::string& label,
                   uint32_t what,
                   const std::vector<const char*>& argv,
                   mxio_flat_namespace_t* flat,
                   mx::channel app_services,
                   mx::channel service_request,
                   mx::vmo data) {
  std::vector<uint32_t> ids;
  std::vector<mx_handle_t> handles;

  if (app_services) {
    ids.push_back(PA_APP_SERVICES);
    handles.push_back(app_services.release());
  }

  if (service_request) {
    ids.push_back(PA_SERVICE_REQUEST);
    handles.push_back(service_request.release());
  }

  for (size_t i = 0; i < flat->count; ++i) {
    ids.push_back(flat->type[i]);
    handles.push_back(flat->handle[i]);
  }

  data.set_property(MX_PROP_NAME, label.data(), label.size());

  // TODO(abarth): We probably shouldn't pass environ, but currently this
  // is very useful as a way to tell the loader in the child process to
  // print out load addresses so we can understand crashes.
  launchpad_t* lp;
  launchpad_create(job.get(), label.c_str(), &lp);
  launchpad_clone(lp, what);
  launchpad_set_args(lp, argv.size(), argv.data());
  launchpad_set_nametable(lp, flat->count, flat->path);
  launchpad_add_handles(lp, handles.size(), handles.data(), ids.data());
  launchpad_load_from_vmo(lp, data.release());

  mx_handle_t proc;
  const char* errmsg;
  auto status = launchpad_go(lp, &proc, &errmsg);
  if (status != MX_OK) {
    FXL_LOG(ERROR) << "Cannot run executable " << label << " due to error "
                   << status << " (" << mx_status_get_string(status)
                   << "): " << errmsg;
    return mx::process();
  }
  return mx::process(proc);
}

mx::process CreateProcess(const mx::job& job,
                          ApplicationPackagePtr package,
                          ApplicationLaunchInfoPtr launch_info,
                          mxio_flat_namespace_t* flat) {
  return Launch(job, GetLabelFromURL(launch_info->url),
                LP_CLONE_MXIO_CWD | LP_CLONE_MXIO_STDIO | LP_CLONE_ENVIRON,
                GetArgv(launch_info), flat, TakeAppServices(launch_info),
                std::move(launch_info->service_request),
                std::move(package->data));
}

mx::process CreateSandboxedProcess(const mx::job& job,
                                   mx::vmo data,
                                   ApplicationLaunchInfoPtr launch_info,
                                   mxio_flat_namespace_t* flat) {
  if (!data)
    return mx::process();

  return Launch(job, GetLabelFromURL(launch_info->url),
                LP_CLONE_MXIO_STDIO | LP_CLONE_ENVIRON, GetArgv(launch_info),
                flat, TakeAppServices(launch_info),
                std::move(launch_info->service_request), std::move(data));
}

LaunchType Classify(const mx::vmo& data, std::string* runner) {
  if (!data)
    return LaunchType::kProcess;
  std::string hint(kMaxShebangLength, '\0');
  size_t count;
  mx_status_t status = data.read(&hint[0], 0, hint.length(), &count);
  if (status != MX_OK)
    return LaunchType::kProcess;
  if (memcmp(hint.data(), &archive::kMagic, sizeof(archive::kMagic)) == 0)
    return LaunchType::kArchive;
  if (hint.find(kFuchsiaMagic) == 0) {
    size_t newline = hint.find('\n', kFuchsiaMagicLength);
    if (newline == std::string::npos)
      return LaunchType::kProcess;
    *runner = hint.substr(kFuchsiaMagicLength, newline - kFuchsiaMagicLength);
    return LaunchType::kRunner;
  }
  return LaunchType::kProcess;
}

}  // namespace

uint32_t ApplicationEnvironmentImpl::next_numbered_label_ = 1u;

ApplicationEnvironmentImpl::ApplicationEnvironmentImpl(
    ApplicationEnvironmentImpl* parent,
    fidl::InterfaceHandle<ApplicationEnvironmentHost> host,
    const fidl::String& label)
    : parent_(parent) {
  host_.Bind(std::move(host));

  // parent_ is null if this is the root application environment. if so, we
  // derive from the application manager's job.
  mx_handle_t parent_job =
      parent_ != nullptr ? parent_->job_.get() : mx_job_default();
  FXL_CHECK(mx::job::create(parent_job, 0u, &job_) == MX_OK);
  FXL_CHECK(job_.duplicate(kChildJobRights, &job_for_child_) == MX_OK);

  // Get the ApplicationLoader service up front.
  ServiceProviderPtr service_provider;
  GetServices(service_provider.NewRequest());
  loader_ = ConnectToService<ApplicationLoader>(service_provider.get());

  if (label.size() == 0)
    label_ = fxl::StringPrintf(kNumberedLabelFormat, next_numbered_label_++);
  else
    label_ = label.get().substr(0, ApplicationEnvironment::kLabelMaxLength);

  mtl::SetObjectName(job_.get(), label_);

  app::ServiceProviderPtr services_backend;
  host_->GetApplicationEnvironmentServices(services_backend.NewRequest());
  services_.set_backend(std::move(services_backend));

  services_.AddService<ApplicationEnvironment>(
      [this](fidl::InterfaceRequest<ApplicationEnvironment> request) {
        environment_bindings_.AddBinding(this, std::move(request));
      });

  services_.AddService<ApplicationLauncher>(
      [this](fidl::InterfaceRequest<ApplicationLauncher> request) {
        launcher_bindings_.AddBinding(this, std::move(request));
      });
}

ApplicationEnvironmentImpl::~ApplicationEnvironmentImpl() {
  job_.kill();
}

std::unique_ptr<ApplicationEnvironmentControllerImpl>
ApplicationEnvironmentImpl::ExtractChild(ApplicationEnvironmentImpl* child) {
  auto it = children_.find(child);
  if (it == children_.end()) {
    return nullptr;
  }
  auto controller = std::move(it->second);
  children_.erase(it);
  return controller;
}

std::unique_ptr<ApplicationControllerImpl>
ApplicationEnvironmentImpl::ExtractApplication(
    ApplicationControllerImpl* controller) {
  auto it = applications_.find(controller);
  if (it == applications_.end()) {
    return nullptr;
  }
  auto application = std::move(it->second);
  applications_.erase(it);
  return application;
}

void ApplicationEnvironmentImpl::AddBinding(
    fidl::InterfaceRequest<ApplicationEnvironment> environment) {
  environment_bindings_.AddBinding(this, std::move(environment));
}

void ApplicationEnvironmentImpl::CreateNestedEnvironment(
    fidl::InterfaceHandle<ApplicationEnvironmentHost> host,
    fidl::InterfaceRequest<ApplicationEnvironment> environment,
    fidl::InterfaceRequest<ApplicationEnvironmentController> controller_request,
    const fidl::String& label) {
  auto controller = std::make_unique<ApplicationEnvironmentControllerImpl>(
      std::move(controller_request),
      std::make_unique<ApplicationEnvironmentImpl>(this, std::move(host),
                                                   label));
  ApplicationEnvironmentImpl* child = controller->environment();
  child->AddBinding(std::move(environment));
  children_.emplace(child, std::move(controller));

  PublishServicesForFirstNestedEnvironment(&child->services_);
}

void ApplicationEnvironmentImpl::GetApplicationLauncher(
    fidl::InterfaceRequest<ApplicationLauncher> launcher) {
  launcher_bindings_.AddBinding(this, std::move(launcher));
}

void ApplicationEnvironmentImpl::GetServices(
    fidl::InterfaceRequest<ServiceProvider> services) {
  services_.AddBinding(std::move(services));
}

void ApplicationEnvironmentImpl::CreateApplication(
    ApplicationLaunchInfoPtr launch_info,
    fidl::InterfaceRequest<ApplicationController> controller) {
  if (launch_info->url.get().empty()) {
    FXL_LOG(ERROR) << "Cannot create application because launch_info contains"
                      " an empty url";
    return;
  }
  std::string canon_url = CanonicalizeURL(launch_info->url);
  if (canon_url.empty()) {
    FXL_LOG(ERROR) << "Cannot run " << launch_info->url
                   << " because the url could not be canonicalized";
    return;
  }
  launch_info->url = canon_url;

  // launch_info is moved before LoadApplication() gets at its first argument.
  fidl::String url = launch_info->url;
  loader_->LoadApplication(
      url, fxl::MakeCopyable([
        this, launch_info = std::move(launch_info),
        controller = std::move(controller)
      ](ApplicationPackagePtr package) mutable {
        if (package) {
          std::string runner;
          LaunchType type = Classify(package->data, &runner);
          switch (type) {
            case LaunchType::kProcess:
              CreateApplicationWithProcess(std::move(package),
                                           std::move(launch_info),
                                           std::move(controller));
              break;
            case LaunchType::kArchive:
              CreateApplicationFromArchive(std::move(package),
                                           std::move(launch_info),
                                           std::move(controller));
              break;
            case LaunchType::kRunner:
              CreateApplicationWithRunner(std::move(package),
                                          std::move(launch_info), runner,
                                          std::move(controller));
              break;
          }
        }
      }));
}

void ApplicationEnvironmentImpl::CreateApplicationWithRunner(
    ApplicationPackagePtr package,
    ApplicationLaunchInfoPtr launch_info,
    std::string runner,
    fidl::InterfaceRequest<ApplicationController> controller) {
  mx::channel svc = services_.OpenAsDirectory();
  if (!svc)
    return;

  NamespaceBuilder builder;
  builder.AddRoot();
  builder.AddServices(std::move(svc));

  // Add the custom namespace.
  // Note that this must be the last |builder| step adding entries to the
  // namespace so that we can filter out entries already added in previous
  // steps.
  builder.AddFlatNamespace(std::move(launch_info->flat_namespace));

  auto startup_info = ApplicationStartupInfo::New();
  startup_info->launch_info = std::move(launch_info);
  startup_info->flat_namespace = builder.BuildForRunner();

  auto* runner_ptr = GetOrCreateRunner(runner);
  if (runner_ptr == nullptr) {
    FXL_LOG(ERROR) << "Could not create runner " << runner << " to run "
                   << launch_info->url;
  }
  runner_ptr->StartApplication(std::move(package), std::move(startup_info),
                               std::move(controller));
}

void ApplicationEnvironmentImpl::CreateApplicationWithProcess(
    ApplicationPackagePtr package,
    ApplicationLaunchInfoPtr launch_info,
    fidl::InterfaceRequest<ApplicationController> controller) {
  mx::channel svc = services_.OpenAsDirectory();
  if (!svc)
    return;

  NamespaceBuilder builder;
  builder.AddRoot();
  builder.AddServices(std::move(svc));

  // Add the custom namespace.
  // Note that this must be the last |builder| step adding entries to the
  // namespace so that we can filter out entries already added in previous
  // steps.
  builder.AddFlatNamespace(std::move(launch_info->flat_namespace));

  const std::string url = launch_info->url;  // Keep a copy before moving it.
  mx::process process = CreateProcess(job_for_child_, std::move(package),
                                      std::move(launch_info), builder.Build());

  if (process) {
    auto application = std::make_unique<ApplicationControllerImpl>(
        std::move(controller), this, nullptr, std::move(process), url);
    ApplicationControllerImpl* key = application.get();
    applications_.emplace(key, std::move(application));
  }
}

void ApplicationEnvironmentImpl::CreateApplicationFromArchive(
    ApplicationPackagePtr package,
    ApplicationLaunchInfoPtr launch_info,
    fidl::InterfaceRequest<ApplicationController> controller) {
  auto file_system =
      std::make_unique<archive::FileSystem>(std::move(package->data));
  mx::channel pkg = file_system->OpenAsDirectory();
  if (!pkg)
    return;
  mx::channel svc = services_.OpenAsDirectory();
  if (!svc)
    return;

  // Note that |builder| is only used in the else block below. It is left here
  // because we would like to use it everywhere once US-313 is fixed.
  NamespaceBuilder builder;
  builder.AddPackage(std::move(pkg));
  builder.AddServices(std::move(svc));

  std::string sandbox_data;
  if (file_system->GetFileAsString(kSandboxPath, &sandbox_data)) {
    SandboxMetadata sandbox;
    if (!sandbox.Parse(sandbox_data)) {
      FXL_LOG(ERROR) << "Failed to parse sandbox metadata for "
                     << launch_info->url;
      return;
    }
    builder.AddSandbox(sandbox);
  }

  // Add the custom namespace.
  // Note that this must be the last |builder| step adding entries to the
  // namespace so that we can filter out entries already added in previous
  // steps.
  builder.AddFlatNamespace(std::move(launch_info->flat_namespace));

  std::string runtime_data;
  if (file_system->GetFileAsString(kRuntimePath, &runtime_data)) {
    RuntimeMetadata runtime;
    if (!runtime.Parse(runtime_data)) {
      FXL_LOG(ERROR) << "Failed to parse runtime metadata for "
                     << launch_info->url;
      return;
    }

    auto inner_package = ApplicationPackage::New();
    inner_package->data = file_system->GetFileAsVMO(kAppPath);
    inner_package->resolved_url = package->resolved_url;

    auto startup_info = ApplicationStartupInfo::New();
    startup_info->launch_info = std::move(launch_info);
    // NOTE: startup_info->flat_namespace is currently (7/2017) mostly ignored
    // by all runners: https://fuchsia.atlassian.net/browse/US-313. They only
    // extract /svc to expose to children through app::ApplicationContext.
    // We would rather expose everything in |builder| as the effective global
    // namespace for each child application.
    auto flat_namespace = FlatNamespace::New();
    flat_namespace->paths.resize(1);
    flat_namespace->paths[0] = "/svc";
    flat_namespace->directories.resize(1);
    flat_namespace->directories[0] = services_.OpenAsDirectory();
    startup_info->flat_namespace = std::move(flat_namespace);

    auto* runner = GetOrCreateRunner(runtime.runner());
    if (runner == nullptr) {
      FXL_LOG(ERROR) << "Cannot create " << runner << " to run "
                     << launch_info->url;
      return;
    }
    runner->StartApplication(std::move(inner_package), std::move(startup_info),
                             std::move(controller));
  } else {
    const std::string url = launch_info->url;  // Keep a copy before moving it.
    mx::process process = CreateSandboxedProcess(
        job_for_child_, file_system->GetFileAsVMO(kAppPath),
        std::move(launch_info), builder.Build());

    if (process) {
      auto application = std::make_unique<ApplicationControllerImpl>(
          std::move(controller), this, std::move(file_system),
          std::move(process), url);
      ApplicationControllerImpl* key = application.get();
      applications_.emplace(key, std::move(application));
    }
  }
}

ApplicationRunnerHolder* ApplicationEnvironmentImpl::GetOrCreateRunner(
    const std::string& runner) {
  // We create the entry in |runners_| before calling ourselves
  // recursively to detect cycles.
  auto result = runners_.emplace(runner, nullptr);
  if (result.second) {
    ServiceProviderPtr runner_services;
    ApplicationControllerPtr runner_controller;
    auto runner_launch_info = ApplicationLaunchInfo::New();
    runner_launch_info->url = runner;
    runner_launch_info->services = runner_services.NewRequest();
    CreateApplication(std::move(runner_launch_info),
                      runner_controller.NewRequest());

    runner_controller.set_connection_error_handler(
        [this, runner] { runners_.erase(runner); });

    result.first->second = std::make_unique<ApplicationRunnerHolder>(
        std::move(runner_services), std::move(runner_controller));
  } else if (!result.first->second) {
    // There was a cycle in the runner graph.
    FXL_LOG(ERROR) << "Detected a cycle in the runner graph for " << runner
                   << ".";
    return nullptr;
  }

  return result.first->second.get();
}

}  // namespace app
