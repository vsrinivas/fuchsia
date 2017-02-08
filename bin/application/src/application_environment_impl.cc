// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/src/application_environment_impl.h"

#include <fcntl.h>
#include <launchpad/launchpad.h>
#include <magenta/processargs.h>
#include <magenta/status.h>
#include <magenta/syscalls.h>
#include <mx/process.h>
#include <mxio/util.h>
#include <unistd.h>

#include <utility>

#include "application/lib/app/connect.h"
#include "application/src/url_resolver.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/strings/string_printf.h"

namespace modular {
namespace {

constexpr char kFuchsiaMagic[] = "#!fuchsia ";
constexpr size_t kFuchsiaMagicLength = sizeof(kFuchsiaMagic) - 1;
constexpr size_t kMaxShebangLength = 2048;
constexpr char kNumberedLabelFormat[] = "env-%d";

bool HasHandle(const fidl::Map<uint32_t, mx::handle<void>>& startup_handles,
               uint32_t handle_id) {
  return startup_handles.find(handle_id) != startup_handles.cend();
}

bool HasReservedHandles(
    const fidl::Map<uint32_t, mx::handle<void>>& startup_handles) {
  return HasHandle(startup_handles, MX_HND_TYPE_APPLICATION_ENVIRONMENT) ||
         HasHandle(startup_handles, MX_HND_TYPE_APPLICATION_SERVICES);
}

mx::process CreateProcess(
    const mx::job& job,
    ApplicationPackagePtr package,
    ApplicationLaunchInfoPtr launch_info,
    fidl::InterfaceHandle<ApplicationEnvironment> environment) {
  fidl::Map<uint32_t, mx::handle<void>> startup_handles =
      std::move(launch_info->startup_handles);
  startup_handles.insert(MX_HND_TYPE_APPLICATION_ENVIRONMENT,
                         environment.PassHandle());
  if (launch_info->services) {
    startup_handles.insert(MX_HND_TYPE_APPLICATION_SERVICES,
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

  const char* path_arg = launch_info->url.get().c_str();
  std::vector<const char*> argv;
  argv.reserve(launch_info->arguments.size() + 1);
  argv.push_back(path_arg);
  for (const auto& argument : launch_info->arguments)
    argv.push_back(argument.get().c_str());

  mx::vmo data = std::move(package->data);

  // TODO(abarth): We probably shouldn't pass environ, but currently this
  // is very useful as a way to tell the loader in the child process to
  // print out load addresses so we can understand crashes.
  // TODO(vardhan): The job passed to the child process (which will be
  // duplicated from this |job|) should not be killable.
  mx_handle_t result = launchpad_launch_mxio_vmo_etc(
      job.get(), path_arg, data.release(), argv.size(), argv.data(), environ,
      handles.size(), handles.data(), ids.data());
  if (result < 0) {
    auto status = static_cast<mx_status_t>(result);
    FTL_LOG(ERROR) << "Cannot run executable " << launch_info->url
                   << " due to error " << status << " ("
                   << mx_status_get_string(status) << ")";
    return mx::process();
  }
  return mx::process(result);
}

bool HasShebang(const mx::vmo& data, std::string* runner) {
  if (!data)
    return false;
  std::string shebang(kMaxShebangLength, '\0');
  size_t count;
  mx_status_t status = data.read(&shebang[0], 0, shebang.length(), &count);
  if (status != NO_ERROR)
    return false;
  if (shebang.find(kFuchsiaMagic) != 0)
    return false;
  size_t newline = shebang.find('\n', kFuchsiaMagicLength);
  if (newline == std::string::npos)
    return false;
  *runner = shebang.substr(kFuchsiaMagicLength, newline - kFuchsiaMagicLength);
  return true;
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
  host_->GetApplicationEnvironmentServices(std::move(services));
}

void ApplicationEnvironmentImpl::Duplicate(
    fidl::InterfaceRequest<ApplicationEnvironment> environment) {
  environment_bindings_.AddBinding(this, std::move(environment));
}

void ApplicationEnvironmentImpl::CreateApplication(
    modular::ApplicationLaunchInfoPtr launch_info,
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
  loader_->LoadApplication(url, ftl::MakeCopyable([
                             this, launch_info = std::move(launch_info),
                             controller = std::move(controller)
                           ](ApplicationPackagePtr package) mutable {
                             std::string runner;
                             if (HasShebang(package->data, &runner)) {
                               CreateApplicationWithRunner(
                                   std::move(package), std::move(launch_info),
                                   runner, std::move(controller));
                             } else {
                               CreateApplicationWithProcess(
                                   std::move(package), std::move(launch_info),
                                   environment_bindings_.AddBinding(this),
                                   std::move(controller));
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
    fidl::InterfaceHandle<ApplicationEnvironment> environment,
    fidl::InterfaceRequest<ApplicationController> controller) {
  const std::string url = launch_info->url;  // Keep a copy before moving it.
  mx::process process = CreateProcess(
      job_, std::move(package), std::move(launch_info), std::move(environment));
  if (process) {
    auto application = std::make_unique<ApplicationControllerImpl>(
        std::move(controller), this, std::move(process), url);
    ApplicationControllerImpl* key = application.get();
    applications_.emplace(key, std::move(application));
  }
}

}  // namespace modular
