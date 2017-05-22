// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/src/manager/application_environment_impl.h"

#include <fcntl.h>
#include <launchpad/launchpad.h>
#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/status.h>
#include <mx/process.h>
#include <mxio/namespace.h>
#include <unistd.h>

#include <utility>

#include "application/lib/app/connect.h"
#include "application/lib/far/format.h"
#include "application/src/manager/url_resolver.h"
#include "lib/ftl/functional/auto_call.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/strings/string_printf.h"
#include "lib/mtl/handles/object_info.h"

namespace app {
namespace {

constexpr char kFuchsiaMagic[] = "#!fuchsia ";
constexpr size_t kFuchsiaMagicLength = sizeof(kFuchsiaMagic) - 1;
constexpr size_t kMaxShebangLength = 2048;
constexpr char kNumberedLabelFormat[] = "env-%d";
constexpr char kAppPath[] = "bin/app";

enum class LaunchType {
  kProcess,
  kArchive,
  kRunner,
};

bool HasHandle(const fidl::Map<uint32_t, mx::handle>& startup_handles,
               uint32_t handle_id) {
  return startup_handles.find(handle_id) != startup_handles.cend();
}

bool HasReservedHandles(
    const fidl::Map<uint32_t, mx::handle>& startup_handles) {
  return HasHandle(startup_handles, MX_HND_TYPE_APPLICATION_ENVIRONMENT) ||
         HasHandle(startup_handles, MX_HND_TYPE_APPLICATION_SERVICES);
}

std::vector<const char*> GetArgv(const ApplicationLaunchInfoPtr& launch_info) {
  std::vector<const char*> argv;
  argv.reserve(launch_info->arguments.size() + 1);
  argv.push_back(launch_info->url.get().c_str());
  for (const auto& argument : launch_info->arguments)
    argv.push_back(argument.get().c_str());
  return argv;
}

// The very first process we create gets the PA_SERVICE_REQUEST given to us by
// our parent. This handle comes from the devmgr and is intended as a short term
// solution for wiring up the graphics driver to the tracing services.
// TODO(abarth): Once namespaces are a thing, switch to a more robust approach.
void ForwardServiceRequestToFirstProcess(std::vector<uint32_t>* ids,
                                         std::vector<mx_handle_t>* handles) {
  static mx_handle_t request = mx_get_startup_handle(PA_SERVICE_REQUEST);
  if (request == MX_HANDLE_INVALID)
    return;
  ids->push_back(PA_SERVICE_REQUEST);
  handles->push_back(request);
  request = MX_HANDLE_INVALID;
}

std::string GetLabelFromURL(const std::string& url) {
  size_t last_slash = url.rfind('/');
  if (last_slash == std::string::npos || last_slash + 1 == url.length())
    return url;
  return url.substr(last_slash + 1);
}

mx::process CreateProcess(const mx::job& job,
                          ApplicationPackagePtr package,
                          ApplicationLaunchInfoPtr launch_info,
                          mx::channel service_root) {
  fidl::Map<uint32_t, mx::handle> startup_handles =
      std::move(launch_info->startup_handles);

  if (service_root)
    startup_handles.insert(PA_SERVICE_ROOT, std::move(service_root));

  if (launch_info->services) {
    startup_handles.insert(PA_APP_SERVICES,
                           launch_info->services.PassChannel());
  }

  std::vector<uint32_t> ids;
  std::vector<mx_handle_t> handles;
  ids.reserve(startup_handles.size());
  handles.reserve(startup_handles.size());
  for (auto it = startup_handles.begin(); it != startup_handles.end(); ++it) {
    ids.push_back(it.GetKey());
    handles.push_back(it.GetValue().release());
  }

  ForwardServiceRequestToFirstProcess(&ids, &handles);

  std::string label = GetLabelFromURL(launch_info->url);
  std::vector<const char*> argv = GetArgv(launch_info);
  mx::vmo data = std::move(package->data);

  // TODO(abarth): We probably shouldn't pass environ, but currently this
  // is very useful as a way to tell the loader in the child process to
  // print out load addresses so we can understand crashes.
  // TODO(vardhan): The job passed to the child process (which will be
  // duplicated from this |job|) should not be killable.
  launchpad_t* lp;
  launchpad_create(job.get(), label.c_str(), &lp);
  launchpad_load_from_vmo(lp, data.release());
  launchpad_set_args(lp, argv.size(), argv.data());
  launchpad_set_environ(lp, environ);
  launchpad_clone(lp, LP_CLONE_MXIO_ALL);
  launchpad_add_handles(lp, handles.size(), handles.data(), ids.data());
  mx_handle_t proc;
  const char* errmsg;
  auto status = launchpad_go(lp, &proc, &errmsg);
  if (status != NO_ERROR) {
    FTL_LOG(ERROR) << "Cannot run executable " << launch_info->url
                   << " due to error " << status << " ("
                   << mx_status_get_string(status) << "): " << errmsg;
    return mx::process();
  }
  return mx::process(proc);
}

mx::process CreateSandboxedProcess(const mx::job& job,
                                   mx::vmo data,
                                   ApplicationLaunchInfoPtr launch_info,
                                   mxio_flat_namespace_t* flat) {
  if (!data)
    return mx::process();

  std::string label = GetLabelFromURL(launch_info->url);
  std::vector<const char*> argv = GetArgv(launch_info);

  // TODO(vardhan): The job passed to the child process (which will be
  // duplicated from this |job|) should not be killable.
  launchpad_t* lp;
  launchpad_create(job.get(), label.c_str(), &lp);
  launchpad_clone(lp, LP_CLONE_MXIO_STDIO | LP_CLONE_ENVIRON);
  launchpad_set_args(lp, argv.size(), argv.data());

  launchpad_set_nametable(lp, flat->count, flat->path);

  std::vector<uint32_t> ids;
  std::vector<mx_handle_t> handles;

  for (size_t i = 0; i < flat->count; ++i) {
    ids.push_back(flat->type[i]);
    handles.push_back(flat->handle[i]);
  }

  if (launch_info->services) {
    ids.push_back(PA_APP_SERVICES);
    handles.push_back(launch_info->services.PassChannel().release());
  }

  launchpad_add_handles(lp, ids.size(), handles.data(), ids.data());
  launchpad_load_from_vmo(lp, data.release());

  mx_handle_t proc;
  const char* errmsg;
  auto status = launchpad_go(lp, &proc, &errmsg);
  if (status != NO_ERROR) {
    FTL_LOG(ERROR) << "Cannot run executable " << launch_info->url
                   << " due to error " << status << " ("
                   << mx_status_get_string(status) << "): " << errmsg;
    return mx::process();
  }
  return mx::process(proc);
}

LaunchType Classify(const mx::vmo& data, std::string* runner) {
  if (!data)
    return LaunchType::kProcess;
  std::string hint(kMaxShebangLength, '\0');
  size_t count;
  mx_status_t status = data.read(&hint[0], 0, hint.length(), &count);
  if (status != NO_ERROR)
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
  FTL_CHECK(mx::job::create(parent_job, 0u, &job_) == NO_ERROR);

  // Get the ApplicationLoader service up front.
  ServiceProviderPtr service_provider;
  GetServices(service_provider.NewRequest());
  loader_ = ConnectToService<ApplicationLoader>(service_provider.get());

  if (label.size() == 0)
    label_ = ftl::StringPrintf(kNumberedLabelFormat, next_numbered_label_++);
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

ApplicationEnvironmentImpl* ApplicationEnvironmentImpl::FindByLabel(
    ftl::StringView label) {
  if (label_ == label)
    return this;
  for (const auto& child : children_) {
    ApplicationEnvironmentImpl* env =
        child.second->environment()->FindByLabel(label);
    if (env)
      return env;
  }
  return nullptr;
}

void ApplicationEnvironmentImpl::Describe(std::ostream& out) {
  out << "Environment " << label_ << " [" << this << "]" << std::endl;

  if (!applications_.empty()) {
    out << "  applications:" << std::endl;
    for (const auto& pair : applications_) {
      ApplicationControllerImpl* app = pair.second.get();
      out << "    - " << app->path() << " [" << app << "]" << std::endl;
    }
  }

  if (!children_.empty()) {
    out << "  children:" << std::endl;
    for (const auto& pair : children_) {
      ApplicationEnvironmentImpl* env = pair.second->environment();
      out << "    - " << env->label() << " [" << env << "]" << std::endl;
    }
  }

  if (!children_.empty()) {
    for (const auto& pair : children_) {
      pair.second->environment()->Describe(out);
    }
  }
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
  child->Duplicate(std::move(environment));
  children_.emplace(child, std::move(controller));
}

void ApplicationEnvironmentImpl::GetApplicationLauncher(
    fidl::InterfaceRequest<ApplicationLauncher> launcher) {
  launcher_bindings_.AddBinding(this, std::move(launcher));
}

void ApplicationEnvironmentImpl::GetServices(
    fidl::InterfaceRequest<ServiceProvider> services) {
  services_.AddBinding(std::move(services));
}

void ApplicationEnvironmentImpl::Duplicate(
    fidl::InterfaceRequest<ApplicationEnvironment> environment) {
  environment_bindings_.AddBinding(this, std::move(environment));
}

void ApplicationEnvironmentImpl::CreateApplication(
    ApplicationLaunchInfoPtr launch_info,
    fidl::InterfaceRequest<ApplicationController> controller) {
  if (launch_info->url.get().empty()) {
    FTL_LOG(ERROR) << "Cannot create application because launch_info contains"
                      " an empty url";
    return;
  }
  std::string canon_url = CanonicalizeURL(launch_info->url);
  if (canon_url.empty()) {
    FTL_LOG(ERROR) << "Cannot run " << launch_info->url
                   << " because the url could not be canonicalized";
    return;
  }
  launch_info->url = canon_url;

  if (HasReservedHandles(launch_info->startup_handles)) {
    FTL_LOG(ERROR)
        << "Cannot run " << launch_info->url
        << " because the caller tried to bind reserved startup handles.";
    return;
  }

  // launch_info is moved before LoadApplication() gets at its first argument.
  fidl::String url = launch_info->url;
  loader_->LoadApplication(
      url, ftl::MakeCopyable([
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
    FTL_LOG(ERROR) << "Cannot run " << launch_info->url << " with " << runner
                   << " because of a cycle in the runner graph.";
    return;
  }

  auto startup_info = ApplicationStartupInfo::New();
  startup_info->environment = environment_bindings_.AddBinding(this);
  startup_info->launch_info = std::move(launch_info);
  result.first->second->StartApplication(
      std::move(package), std::move(startup_info), std::move(controller));
}

void ApplicationEnvironmentImpl::CreateApplicationWithProcess(
    ApplicationPackagePtr package,
    ApplicationLaunchInfoPtr launch_info,
    fidl::InterfaceRequest<ApplicationController> controller) {
  const std::string url = launch_info->url;  // Keep a copy before moving it.
  mx::process process =
      CreateProcess(job_, std::move(package), std::move(launch_info),
                    services_.OpenAsDirectory());
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

  mx_handle_t handles[] = {pkg.get(), svc.get()};
  uint32_t types[] = {PA_HND(PA_NS_DIR, 0), PA_HND(PA_NS_DIR, 1)};
  const char* paths[] = {"/pkg", "/svc"};

  mxio_flat_namespace_t flat;
  flat.count = 2;
  flat.handle = handles;
  flat.type = types;
  flat.path = paths;

  const std::string url = launch_info->url;  // Keep a copy before moving it.
  mx::process process = CreateSandboxedProcess(
      job_, file_system->GetFileAsVMO(kAppPath), std::move(launch_info), &flat);

  if (process) {
    auto application = std::make_unique<ApplicationControllerImpl>(
        std::move(controller), this, std::move(file_system), std::move(process),
        url);
    ApplicationControllerImpl* key = application.get();
    applications_.emplace(key, std::move(application));
  }
}

}  // namespace app
