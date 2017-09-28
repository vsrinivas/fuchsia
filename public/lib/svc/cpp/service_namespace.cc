// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/svc/cpp/service_namespace.h"

#include <fcntl.h>
#include <fdio/util.h>
#include <zircon/device/vfs.h>

#include <utility>

#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/files/unique_fd.h"

namespace app {

ServiceNamespace::ServiceNamespace()
    : vfs_(fsl::MessageLoop::GetCurrent()->async()),
      directory_(fbl::AdoptRef(new svcfs::VnodeDir())) {}

ServiceNamespace::ServiceNamespace(
    fidl::InterfaceRequest<app::ServiceProvider> request)
    : ServiceNamespace() {
  AddBinding(std::move(request));
}

ServiceNamespace::~ServiceNamespace() {
  directory_->RemoveAllServices();
  directory_ = nullptr;
}

void ServiceNamespace::AddBinding(
    fidl::InterfaceRequest<app::ServiceProvider> request) {
  if (request)
    bindings_.AddBinding(this, std::move(request));
}

void ServiceNamespace::Close() {
  bindings_.CloseAllBindings();
}

void ServiceNamespace::AddServiceForName(ServiceConnector connector,
                                         const std::string& service_name) {
  name_to_service_connector_[service_name] = std::move(connector);
  directory_->AddService(fbl::StringPiece(service_name.data(), service_name.length()), this);
}

void ServiceNamespace::RemoveServiceForName(const std::string& service_name) {
  auto it = name_to_service_connector_.find(service_name);
  if (it != name_to_service_connector_.end())
    name_to_service_connector_.erase(it);
  directory_->RemoveService(fbl::StringPiece(service_name.data(), service_name.length()));
}

bool ServiceNamespace::ServeDirectory(zx::channel channel) {
  return vfs_.ServeDirectory(directory_, std::move(channel)) == ZX_OK;
}

int ServiceNamespace::OpenAsFileDescriptor() {
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

bool ServiceNamespace::MountAtPath(const char* path) {
  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) < 0)
    return false;

  if (!ServeDirectory(std::move(h1)))
    return false;

  fxl::UniqueFD fd(open(path, O_DIRECTORY | O_RDWR));
  if (fd.get() < 0)
    return false;

  zx_handle_t h = h2.release();
  return ioctl_vfs_mount_fs(fd.get(), &h) >= 0;
}

void ServiceNamespace::Connect(fbl::StringPiece name,
                               zx::channel channel) {
  ConnectCommon(std::string(name.data(), name.length()), std::move(channel));
}

void ServiceNamespace::ConnectToService(const fidl::String& service_name,
                                        zx::channel channel) {
  ConnectCommon(service_name, std::move(channel));
}

void ServiceNamespace::ConnectCommon(const std::string& service_name,
                                     zx::channel channel) {
  auto it = name_to_service_connector_.find(service_name);
  if (it != name_to_service_connector_.end())
    it->second(std::move(channel));
}

}  // namespace app
