// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/svc/cpp/service_provider_bridge.h"

#include <fcntl.h>
#include <lib/fdio/util.h>
#include <fs/service.h>
#include <lib/async/default.h>
#include <zircon/device/vfs.h>

#include <utility>

namespace component {

ServiceProviderBridge::ServiceProviderBridge()
    : vfs_(async_get_default_dispatcher()), weak_factory_(this) {
  directory_ =
      fbl::AdoptRef(new ServiceProviderDir(weak_factory_.GetWeakPtr()));
}

ServiceProviderBridge::~ServiceProviderBridge() = default;

void ServiceProviderBridge::AddBinding(
    fidl::InterfaceRequest<ServiceProvider> request) {
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

void ServiceProviderBridge::ConnectToService(fidl::StringPtr service_name,
                                             zx::channel channel) {
  auto it = name_to_service_connector_.find(service_name.get());
  if (it != name_to_service_connector_.end())
    it->second(std::move(channel));
  else if (backend_)
    backend_->ConnectToService(*service_name, std::move(channel));
  else if (backing_dir_)
    fdio_service_connect_at(backing_dir_.get(), service_name->c_str(),
                            channel.release());
}

ServiceProviderBridge::ServiceProviderDir::ServiceProviderDir(
    fxl::WeakPtr<ServiceProviderBridge> bridge)
    : bridge_(std::move(bridge)) {}

ServiceProviderBridge::ServiceProviderDir::~ServiceProviderDir() = default;

zx_status_t ServiceProviderBridge::ServiceProviderDir::Lookup(
    fbl::RefPtr<fs::Vnode>* out,
    fbl::StringPiece name) {
  *out = fbl::AdoptRef(new fs::Service(
      [bridge = bridge_,
       name = std::string(name.data(), name.length())](zx::channel channel) {
        if (bridge) {
          bridge->ConnectToService(name, std::move(channel));
          return ZX_OK;
        }
        return ZX_ERR_NOT_FOUND;
      }));
  return ZX_OK;
}

zx_status_t ServiceProviderBridge::ServiceProviderDir::Getattr(vnattr_t* attr) {
  memset(attr, 0, sizeof(vnattr_t));
  attr->mode = V_TYPE_DIR | V_IRUSR;
  attr->nlink = 1;
  return ZX_OK;
}

}  // namespace component
