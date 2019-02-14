// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/service_directory.h>

#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>

namespace sys {
namespace {

zx::channel OpenServiceRoot() {
  zx::channel request, service_root;
  if (zx::channel::create(0, &request, &service_root) != ZX_OK)
    return zx::channel();
  if (fdio_service_connect("/svc/.", request.release()) != ZX_OK)
    return zx::channel();
  return service_root;
}

}  // namespace

ServiceDirectory::ServiceDirectory(zx::channel directory)
    : directory_(std::move(directory)) {}

ServiceDirectory::ServiceDirectory(
    fidl::InterfaceHandle<fuchsia::io::Directory> directory)
    : ServiceDirectory(directory.TakeChannel()) {}

ServiceDirectory::~ServiceDirectory() = default;

std::shared_ptr<ServiceDirectory> ServiceDirectory::CreateFromNamespace() {
  return std::make_shared<ServiceDirectory>(OpenServiceRoot());
}

zx_status_t ServiceDirectory::Connect(const std::string& interface_name,
                                      zx::channel channel) const {
  return fdio_service_connect_at(directory_.get(), interface_name.c_str(),
                                 channel.release());
}

}  // namespace sys
