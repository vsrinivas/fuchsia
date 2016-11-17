// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/application_manager/application_environment_impl.h"

#include <fcntl.h>
#include <launchpad/launchpad.h>
#include <magenta/processargs.h>
#include <magenta/status.h>
#include <magenta/syscalls.h>
#include <mx/process.h>
#include <mxio/util.h>
#include <unistd.h>

#include <utility>

#include "apps/modular/src/application_manager/url_resolver.h"
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
    const std::string& path,
    fidl::InterfaceHandle<ApplicationEnvironment> environment,
    ApplicationLaunchInfoPtr launch_info) {
  fidl::Map<uint32_t, mx::handle<void>> startup_handles =
      std::move(launch_info->startup_handles);
  startup_handles.insert(MX_HND_TYPE_APPLICATION_ENVIRONMENT,
                         environment.PassHandle());
  if (launch_info->services) {
    startup_handles.insert(MX_HND_TYPE_APPLICATION_SERVICES,
                           launch_info->services.PassMessagePipe());
  }

  std::vector<uint32_t> ids;
  std::vector<mx_handle_t> handles;
  ids.reserve(startup_handles.size());
  handles.reserve(startup_handles.size());
  for (auto it = startup_handles.begin(); it != startup_handles.end(); ++it) {
    ids.push_back(it.GetKey());
    handles.push_back(it.GetValue().release());
  }

  const char* path_arg = path.c_str();
  std::vector<const char*> argv;
  argv.reserve(launch_info->arguments.size() + 1);
  argv.push_back(path_arg);
  for (const auto& argument : launch_info->arguments)
    argv.push_back(argument.get().c_str());

  // TODO(abarth): We probably shouldn't pass environ, but currently this
  // is very useful as a way to tell the loader in the child process to
  // print out load addresses so we can understand crashes.
  mx_handle_t result =
      launchpad_launch_mxio_etc(path_arg, argv.size(), argv.data(), environ,
                                handles.size(), handles.data(), ids.data());
  if (result < 0) {
    auto status = static_cast<mx_status_t>(result);
    FTL_LOG(ERROR) << "Cannot run executable " << path << " due to error "
                   << status << " (" << mx_status_get_string(status) << ")";
    return mx::process();
  }
  return mx::process(result);
}

bool HasShebang(const std::string& path,
                ftl::UniqueFD* result_fd,
                std::string* runner) {
  ftl::UniqueFD fd(open(path.c_str(), O_RDONLY));
  if (!fd.is_valid())
    return false;
  std::string shebang(kMaxShebangLength, '\0');
  ssize_t count = read(fd.get(), &shebang[0], shebang.length());
  if (count == -1)
    return false;
  if (shebang.find(kFuchsiaMagic) != 0)
    return false;
  size_t newline = shebang.find('\n', kFuchsiaMagicLength);
  if (newline == std::string::npos)
    return false;
  if (lseek(fd.get(), 0, SEEK_SET) == -1)
    return false;
  *result_fd = std::move(fd);
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

  if (label.size() == 0)
    label_ = ftl::StringPrintf(kNumberedLabelFormat, next_numbered_label_++);
  else
    label_ = label.get().substr(0, ApplicationEnvironment::kLabelMaxLength);
}

ApplicationEnvironmentImpl::~ApplicationEnvironmentImpl() = default;

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

  std::string path = GetPathFromURL(launch_info->url);
  if (path.empty()) {
    // TODO(abarth): Support URL schemes other than file:// by querying the host
    // for an application runner.
    FTL_LOG(ERROR) << "Cannot run " << launch_info->url
                   << " because the scheme is not supported.";
    return;
  }

  ftl::UniqueFD fd;
  std::string runner;
  if (HasShebang(path, &fd, &runner)) {
    // We create the entry in |runners_| before calling ourselves recursively
    // to detect cycles.
    auto it = runners_.emplace(runner, nullptr);
    if (it.second) {
      ServiceProviderPtr runner_services;
      ApplicationControllerPtr runner_controller;
      auto runner_launch_info = ApplicationLaunchInfo::New();
      runner_launch_info->url = runner;
      runner_launch_info->services = fidl::GetProxy(&runner_services);
      CreateApplication(std::move(runner_launch_info),
                        fidl::GetProxy(&runner_controller));

      runner_controller.set_connection_error_handler(
          [this, runner]() { runners_.erase(runner); });

      it.first->second = std::make_unique<ApplicationRunnerHolder>(
          std::move(runner_services), std::move(runner_controller));
    } else if (!it.first->second) {
      // There was a cycle in the runner graph.
      FTL_LOG(ERROR) << "Cannot run " << launch_info->url << " with " << runner
                     << " because of a cycle in the runner graph.";
      return;
    }

    ApplicationStartupInfoPtr startup_info = ApplicationStartupInfo::New();
    startup_info->environment = environment_bindings_.AddBinding(this);
    startup_info->launch_info = std::move(launch_info);
    it.first->second->StartApplication(std::move(fd), std::move(startup_info),
                                       std::move(controller));
    return;
  }

  CreateApplicationWithProcess(path, environment_bindings_.AddBinding(this),
                               std::move(launch_info), std::move(controller));
}

void ApplicationEnvironmentImpl::CreateApplicationWithProcess(
    const std::string& path,
    fidl::InterfaceHandle<ApplicationEnvironment> environment,
    ApplicationLaunchInfoPtr launch_info,
    fidl::InterfaceRequest<ApplicationController> controller) {
  mx::process process =
      CreateProcess(path, std::move(environment), std::move(launch_info));
  if (process) {
    auto application = std::make_unique<ApplicationControllerImpl>(
        std::move(controller), this, std::move(process), path);
    ApplicationControllerImpl* key = application.get();
    applications_.emplace(key, std::move(application));
  }
}

}  // namespace modular
