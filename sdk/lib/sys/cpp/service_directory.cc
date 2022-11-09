// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/directory.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/channel.h>

namespace sys {
namespace {

zx::channel OpenServiceRoot() {
  zx::channel request, service_root;
  if (zx::channel::create(0, &request, &service_root) != ZX_OK)
    return zx::channel();
  if (fdio_service_connect("/svc", request.release()) != ZX_OK)
    return zx::channel();
  return service_root;
}

}  // namespace

ServiceDirectory::ServiceDirectory(zx::channel directory)
    : ServiceDirectory(fidl::InterfaceHandle<fuchsia::io::Directory>(std::move(directory))) {}

ServiceDirectory::ServiceDirectory(fidl::InterfaceHandle<fuchsia::io::Directory> directory)
    : directory_(directory.BindSync()) {}

ServiceDirectory::~ServiceDirectory() = default;

std::shared_ptr<ServiceDirectory> ServiceDirectory::CreateFromNamespace() {
  return std::make_shared<ServiceDirectory>(OpenServiceRoot());
}

std::shared_ptr<ServiceDirectory> ServiceDirectory::CreateWithRequest(zx::channel* out_request) {
  zx::channel directory;
  // no need to check status, even if this fails, service directory would be
  // backed by invalid channel and Connect will return correct error.
  zx::channel::create(0, &directory, out_request);

  return std::make_shared<ServiceDirectory>(ServiceDirectory(std::move(directory)));
}

std::shared_ptr<ServiceDirectory> ServiceDirectory::CreateWithRequest(
    fidl::InterfaceRequest<fuchsia::io::Directory>* out_request) {
  zx::channel request;
  auto directory = CreateWithRequest(&request);
  out_request->set_channel(std::move(request));
  return directory;
}

zx_status_t ServiceDirectory::Connect(const std::string& interface_name,
                                      zx::channel request) const {
  return fdio_service_connect_at(directory_.unowned_channel()->get(), interface_name.c_str(),
                                 request.release());
}

fidl::InterfaceHandle<fuchsia::io::Directory> ServiceDirectory::CloneChannel() const {
  fidl::InterfaceHandle<fuchsia::io::Directory> dir;
  CloneChannel(dir.NewRequest());
  return dir;
}

zx_status_t ServiceDirectory::CloneChannel(
    fidl::InterfaceRequest<fuchsia::io::Directory> dir) const {
  if (!directory_.is_bound()) {
    return ZX_ERR_BAD_HANDLE;
  }
  return directory_->Clone(fuchsia::io::OpenFlags::CLONE_SAME_RIGHTS,
                           fidl::InterfaceRequest<fuchsia::io::Node>(dir.TakeChannel()));
}

}  // namespace sys
