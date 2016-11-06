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

namespace modular {
namespace {

constexpr char kFuchsiaMagic[] = "#!fuchsia ";
constexpr size_t kFuchsiaMagicLength = sizeof(kFuchsiaMagic) - 1;
constexpr size_t kMaxShebangLength = 2048;

constexpr size_t kSubprocessHandleCount = 2;

mx::process CreateProcess(
    const std::string& path,
    fidl::Array<fidl::String> arguments,
    fidl::InterfaceHandle<ServiceProvider> incoming_services,
    fidl::InterfaceRequest<ServiceProvider> outgoing_services) {
  const char* path_arg = path.c_str();

  mx_handle_t handles[kSubprocessHandleCount]{
      static_cast<mx_handle_t>(incoming_services.PassHandle().release()),
      static_cast<mx_handle_t>(outgoing_services.PassMessagePipe().release()),
  };

  uint32_t ids[kSubprocessHandleCount] = {
      MX_HND_TYPE_INCOMING_SERVICES, MX_HND_TYPE_OUTGOING_SERVICES,
  };

  size_t count = 0;
  while (count < kSubprocessHandleCount) {
    if (handles[count] == MX_HANDLE_INVALID)
      break;
    ++count;
  }

  std::vector<const char*> argv;
  argv.reserve(arguments.size() + 1);
  argv.push_back(path_arg);
  for (const auto& argument : arguments)
    argv.push_back(argument.get().c_str());

  // TODO(abarth): We shouldn't pass stdin, stdout, stderr, or the file system
  // when launching Mojo applications. We probably shouldn't pass environ, but
  // currently this is very useful as a way to tell the loader in the child
  // process to print out load addresses so we can understand crashes.
  mx_handle_t result = launchpad_launch_mxio_etc(
      path_arg, argv.size(), argv.data(), environ, count, handles, ids);
  if (result < 0) {
    auto status = static_cast<mx_status_t>(result);
    FTL_LOG(ERROR) << "Cannot run executable " << path_arg << " due to error "
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

ApplicationEnvironmentImpl::ApplicationEnvironmentImpl(
    ApplicationEnvironmentImpl* parent,
    fidl::InterfaceHandle<ApplicationEnvironmentHost> host)
    : parent_(parent) {
  host_.Bind(std::move(host));
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

void ApplicationEnvironmentImpl::CreateNestedEnvironment(
    fidl::InterfaceHandle<ApplicationEnvironmentHost> host,
    fidl::InterfaceRequest<ApplicationEnvironment> environment,
    fidl::InterfaceRequest<ApplicationEnvironmentController>
        controller_request) {
  auto controller = std::make_unique<ApplicationEnvironmentControllerImpl>(
      std::move(controller_request),
      std::make_unique<ApplicationEnvironmentImpl>(this, std::move(host)));
  ApplicationEnvironmentImpl* child = controller->environment();
  child->Duplicate(std::move(environment));
  children_.emplace(child, std::move(controller));
}

void ApplicationEnvironmentImpl::GetApplicationLauncher(
    fidl::InterfaceRequest<ApplicationLauncher> launcher) {
  launcher_bindings_.AddBinding(this, std::move(launcher));
}

void ApplicationEnvironmentImpl::Duplicate(
    fidl::InterfaceRequest<ApplicationEnvironment> environment) {
  environment_bindings_.AddBinding(this, std::move(environment));
}

void ApplicationEnvironmentImpl::CreateApplication(
    modular::ApplicationLaunchInfoPtr launch_info,
    fidl::InterfaceRequest<ApplicationController> controller) {
  fidl::InterfaceHandle<ServiceProvider> environment_services;
  host_->GetApplicationEnvironmentServices(launch_info->url,
                                           GetProxy(&environment_services));

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
    startup_info->url = launch_info->url;
    startup_info->arguments = std::move(launch_info->arguments);
    startup_info->environment_services = std::move(environment_services);
    startup_info->outgoing_services = std::move(launch_info->services);
    it.first->second->StartApplication(std::move(fd), std::move(startup_info),
                                       std::move(controller));
    return;
  }

  CreateApplicationWithProcess(
      path, std::move(launch_info->arguments), std::move(environment_services),
      std::move(launch_info->services), std::move(controller));
}

void ApplicationEnvironmentImpl::CreateApplicationWithProcess(
    const std::string& path,
    fidl::Array<fidl::String> arguments,
    fidl::InterfaceHandle<ServiceProvider> environment_services,
    fidl::InterfaceRequest<ServiceProvider> services,
    fidl::InterfaceRequest<ApplicationController> controller) {
  mx::process process =
      CreateProcess(path, std::move(arguments), std::move(environment_services),
                    std::move(services));
  if (process) {
    auto application = std::make_unique<ApplicationControllerImpl>(
        std::move(controller), this, std::move(process));
    ApplicationControllerImpl* key = application.get();
    applications_.emplace(key, std::move(application));
  }
}

}  // namespace modular
