// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/svc/service_namespace.h"

#include <fcntl.h>
#include <magenta/device/devmgr.h>
#include <mxio/util.h>
#include <utility>

#include "lib/ftl/files/unique_fd.h"
#include "lib/mtl/vfs/vfs_handler.h"

namespace app {

ServiceNamespace::ServiceNamespace()
    : directory_(new svcfs::VnodeDir(mtl::VFSHandler::Start)) {
  directory_->RefAcquire();
}

ServiceNamespace::ServiceNamespace(
    fidl::InterfaceRequest<app::ServiceProvider> request)
    : directory_(new svcfs::VnodeDir(mtl::VFSHandler::Start)) {
  directory_->RefAcquire();
  AddBinding(std::move(request));
}

ServiceNamespace::~ServiceNamespace() {
  directory_->RemoveAllServices();
  directory_->RefRelease();
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
  directory_->AddService(service_name.data(), service_name.length(), this);
}

mx::channel ServiceNamespace::CloneDirectory() {
  mx::channel h1, h2;
  if (mx::channel::create(0, &h1, &h2) < 0)
    return mx::channel();

  if (directory_->Open(O_DIRECTORY) < 0)
    return mx::channel();

  if (directory_->Serve(h1.release(), 0) < 0) {
    directory_->Close();
    return mx::channel();
  }

  // Setting this signal indicates that this directory is actively being served.
  h2.signal(0, MX_USER_SIGNAL_0);
  return h2;
}

int ServiceNamespace::OpenAsFileDescriptor() {
  mx::channel directory = CloneDirectory();
  if (!directory)
    return -1;
  mxio_t* io = mxio_remote_create(directory.release(), MX_HANDLE_INVALID);
  if (!io)
    return -1;
  return mxio_bind_to_fd(io, -1, 0);
}

bool ServiceNamespace::MountAtPath(const char* path) {
  mx::channel dir = CloneDirectory();
  if (!dir)
    return false;

  ftl::UniqueFD fd(open(path, O_DIRECTORY | O_RDWR));
  if (fd.get() < 0)
    return false;

  mx_handle_t h = dir.release();
  return ioctl_devmgr_mount_fs(fd.get(), &h) >= 0;
}

void ServiceNamespace::Connect(const char* name, size_t len,
                               mx::channel channel) {
  ConnectCommon(std::string(name, len), std::move(channel));
}

void ServiceNamespace::ConnectToService(const fidl::String& service_name,
                                        mx::channel channel) {
  ConnectCommon(service_name, std::move(channel));
}

void ServiceNamespace::ConnectCommon(const std::string& service_name,
                                     mx::channel channel) {
  auto it = name_to_service_connector_.find(service_name);
  if (it != name_to_service_connector_.end())
    it->second(std::move(channel));
}

}  // namespace app
