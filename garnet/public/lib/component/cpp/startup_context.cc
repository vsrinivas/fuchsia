// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/component/cpp/startup_context.h"

#include <lib/async/default.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include "lib/component/cpp/connect.h"

namespace component {

namespace {

constexpr char kServiceRootPath[] = "/svc";

}  // namespace

StartupContext::StartupContext(zx::channel service_root, zx::channel directory_request)
    : incoming_services_(std::make_shared<Services>()) {
  incoming_services_->Bind(std::move(service_root));
  outgoing_.Serve(std::move(directory_request));
}

StartupContext::~StartupContext() = default;

// static
std::unique_ptr<StartupContext> StartupContext::CreateFromStartupInfo() {
  zx_handle_t directory_request = zx_take_startup_handle(PA_DIRECTORY_REQUEST);
  return std::make_unique<StartupContext>(subtle::CreateStaticServiceRootHandle(),
                                          zx::channel(directory_request));
}

std::unique_ptr<StartupContext> StartupContext::CreateFrom(fuchsia::sys::StartupInfo startup_info) {
  fuchsia::sys::FlatNamespace& flat = startup_info.flat_namespace;
  if (flat.paths.size() != flat.directories.size())
    return nullptr;

  zx::channel service_root;
  for (size_t i = 0; i < flat.paths.size(); ++i) {
    if (flat.paths.at(i) == kServiceRootPath) {
      service_root = std::move(flat.directories.at(i));
      break;
    }
  }

  return std::make_unique<StartupContext>(std::move(service_root),
                                          std::move(startup_info.launch_info.directory_request));
}

const fuchsia::sys::EnvironmentPtr& StartupContext::environment() const {
  std::lock_guard<std::mutex> guard(services_mutex_);
  if (!environment_) {
    incoming_services_->ConnectToService(environment_.NewRequest());
  }
  return environment_;
}

const fuchsia::sys::LauncherPtr& StartupContext::launcher() const {
  std::lock_guard<std::mutex> guard(services_mutex_);
  if (!launcher_) {
    incoming_services_->ConnectToService(launcher_.NewRequest());
  }
  return launcher_;
}

void StartupContext::ConnectToEnvironmentService(const std::string& interface_name,
                                                 zx::channel channel) {
  incoming_services()->ConnectToService(std::move(channel), interface_name);
}

namespace subtle {

// static
zx::channel CreateStaticServiceRootHandle() {
  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) != ZX_OK)
    return zx::channel();
  // TODO(abarth): Use kServiceRootPath once that actually works.
  if (fdio_service_connect("/svc/.", h1.release()) != ZX_OK)
    return zx::channel();
  return h2;
}

}  // namespace subtle

}  // namespace component
