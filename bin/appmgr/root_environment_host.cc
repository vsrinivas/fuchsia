// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/root_environment_host.h"

#include <errno.h>
#include <fcntl.h>

#include <utility>

#include "lib/app/fidl/application_environment.fidl.h"
#include "lib/fxl/files/unique_fd.h"

namespace app {
namespace {

constexpr char kRootLabel[] = "root";

}  // namespace

RootEnvironmentHost::RootEnvironmentHost(
    std::vector<std::string> application_path,
    fs::Vfs* vfs)
    : loader_(application_path), host_binding_(this), vfs_(vfs) {
  f1dl::InterfaceHandle<ApplicationEnvironmentHost> host;
  host_binding_.Bind(host.NewRequest());
  root_job_ =
      std::make_unique<JobHolder>(nullptr, vfs_, std::move(host), kRootLabel);
}

RootEnvironmentHost::~RootEnvironmentHost() = default;

void RootEnvironmentHost::GetApplicationEnvironmentServices(
    f1dl::InterfaceRequest<ServiceProvider> environment_services) {
  service_provider_bindings_.AddBinding(this, std::move(environment_services));
}

void RootEnvironmentHost::ConnectToService(const f1dl::String& interface_name,
                                           zx::channel channel) {
  if (interface_name == ApplicationLoader::Name_) {
    loader_bindings_.AddBinding(
        &loader_,
        f1dl::InterfaceRequest<ApplicationLoader>(std::move(channel)));
  }
}

}  // namespace app
