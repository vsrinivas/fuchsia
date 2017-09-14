// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/svc/cpp/service_provider_bridge.h"

#include <fcntl.h>
#include <zircon/device/vfs.h>
#include <fdio/util.h>

#include <utility>

namespace app {

ServiceProviderBridge::ServiceProviderBridge()
    : vfs_(&dispatcher_), directory_(fbl::AdoptRef(new svcfs::VnodeProviderDir())) {
  directory_->SetServiceProvider(this);
}

ServiceProviderBridge::~ServiceProviderBridge() {
  directory_->SetServiceProvider(nullptr);
}

void ServiceProviderBridge::AddBinding(
    fidl::InterfaceRequest<app::ServiceProvider> request) {
  bindings_.AddBinding(this, std::move(request));
}

void ServiceProviderBridge::AddServiceForName(ServiceConnector connector,
                                              const std::string& service_name) {
  name_to_service_connector_[service_name] = std::move(connector);
}

bool ServiceProviderBridge::ServeDirectory(zx::channel channel) {
  return vfs_.ServeDirectory(directory_, std::move(channel)) == ZX_OK;
}

zx::channel ServiceProviderBridge::OpenAsDirectory() {
  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) < 0)
    return zx::channel();
  if (!ServeDirectory(std::move(h1)))
    return zx::channel();
  return h2;
}

int ServiceProviderBridge::OpenAsFileDescriptor() {
  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) < 0)
    return -1;
  if (!ServeDirectory(std::move(h1)))
    return -1;
  fdio_t* io = fdio_remote_create(h2.release(), ZX_HANDLE_INVALID);
  if (!io)
    return -1;
  return fdio_bind_to_fd(io, -1, 0);
}

void ServiceProviderBridge::Connect(const char* name,
                                    size_t len,
                                    zx::channel channel) {
  ConnectToService(fidl::String(name, len), std::move(channel));
}

void ServiceProviderBridge::ConnectToService(const fidl::String& service_name,
                                             zx::channel channel) {
  auto it = name_to_service_connector_.find(service_name.get());
  if (it != name_to_service_connector_.end())
    it->second(std::move(channel));
  else
    backend_->ConnectToService(std::move(service_name), std::move(channel));
}

}  // namespace app
