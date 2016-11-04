// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/application_manager/application_environment_impl.h"

#include <launchpad/launchpad.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <mx/process.h>
#include <mxio/util.h>

#include <utility>

#include "apps/modular/application_manager/url_resolver.h"

namespace modular {
namespace {

constexpr size_t kSubprocessHandleCount = 2;

mx::process CreateProcess(
    const std::string& url,
    fidl::InterfaceHandle<ServiceProvider> incoming_services,
    fidl::InterfaceRequest<ServiceProvider> outgoing_services) {
  std::string path = GetPathFromURL(url);
  if (path.empty())
    return mx::process();

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

  // TODO(abarth): We shouldn't pass stdin, stdout, stderr, or the file system
  // when launching Mojo applications. We probably shouldn't pass environ, but
  // currently this is very useful as a way to tell the loader in the child
  // process to print out load addresses so we can understand crashes.
  mx_handle_t result = launchpad_launch_mxio_etc(path_arg, 1, &path_arg,
                                                 environ, count, handles, ids);

  return result < 0 ? mx::process() : mx::process(result);
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
    const fidl::String& url,
    fidl::InterfaceRequest<ServiceProvider> services,
    fidl::InterfaceRequest<ApplicationController> controller) {
  fidl::InterfaceHandle<ServiceProvider> environment_services;
  host_->GetApplicationEnvironmentServices(url,
                                           GetProxy(&environment_services));
  mx::process process =
      CreateProcess(url, std::move(environment_services), std::move(services));
  if (process) {
    auto application = std::make_unique<ApplicationControllerImpl>(
        std::move(controller), this, std::move(process));
    ApplicationControllerImpl* key = application.get();
    applications_.emplace(key, std::move(application));
  }
}

}  // namespace modular
