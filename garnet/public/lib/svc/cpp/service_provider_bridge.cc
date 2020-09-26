// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/svc/cpp/service_provider_bridge.h"

#include <fcntl.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <zircon/device/vfs.h>

#include <utility>

#include <fs/service.h>
#include <fs/vfs_types.h>

namespace component {

ServiceProviderBridge::ServiceProviderBridge()
    : vfs_(async_get_default_dispatcher()), weak_factory_(this) {
  directory_ = fbl::AdoptRef(new ServiceProviderDir(weak_factory_.GetWeakPtr()));
}

ServiceProviderBridge::~ServiceProviderBridge() = default;

void ServiceProviderBridge::AddBinding(fidl::InterfaceRequest<ServiceProvider> request) {
  bindings_.AddBinding(this, std::move(request));
}

fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> ServiceProviderBridge::AddBinding() {
  return bindings_.AddBinding(this);
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
  int fd = -1;
  zx_status_t status = fdio_fd_create(h2.release(), &fd);
  if (status != ZX_OK)
    return -1;
  return fd;
}

void ServiceProviderBridge::ConnectToService(std::string service_name, zx::channel channel) {
  auto it = name_to_service_connector_.find(service_name);
  if (it != name_to_service_connector_.end())
    it->second(std::move(channel));
  else if (backend_)
    backend_->ConnectToService(service_name, std::move(channel));
  else if (backing_dir_)
    fdio_service_connect_at(backing_dir_.get(), service_name.c_str(), channel.release());
}

ServiceProviderBridge::ServiceProviderDir::ServiceProviderDir(
    fxl::WeakPtr<ServiceProviderBridge> bridge)
    : bridge_(std::move(bridge)) {}

ServiceProviderBridge::ServiceProviderDir::~ServiceProviderDir() = default;

fs::VnodeProtocolSet ServiceProviderBridge::ServiceProviderDir::GetProtocols() const {
  return fs::VnodeProtocol::kDirectory;
}

zx_status_t ServiceProviderBridge::ServiceProviderDir::Lookup(fbl::StringPiece name,
                                                              fbl::RefPtr<fs::Vnode>* out) {
  *out = fbl::AdoptRef(new fs::Service(
      [bridge = bridge_, name = std::string(name.data(), name.length())](zx::channel channel) {
        if (bridge) {
          bridge->ConnectToService(name, std::move(channel));
          return ZX_OK;
        }
        return ZX_ERR_NOT_FOUND;
      }));
  return ZX_OK;
}

zx_status_t ServiceProviderBridge::ServiceProviderDir::GetAttributes(fs::VnodeAttributes* attr) {
  *attr = fs::VnodeAttributes();
  attr->mode = V_TYPE_DIR | V_IRUSR;
  attr->link_count = 1;
  return ZX_OK;
}

zx_status_t ServiceProviderBridge::ServiceProviderDir::GetNodeInfoForProtocol(
    [[maybe_unused]] fs::VnodeProtocol protocol, [[maybe_unused]] fs::Rights rights,
    fs::VnodeRepresentation* representation) {
  *representation = fs::VnodeRepresentation::Directory();
  return ZX_OK;
}

}  // namespace component
