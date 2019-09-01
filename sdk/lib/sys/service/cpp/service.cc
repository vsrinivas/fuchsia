// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/sys/service/cpp/service.h>

namespace sys {

namespace {

// An implementation of a ServiceConnector based on fuchsia.io.Directory.
class DirectoryServiceConnector final : public fidl::ServiceConnector {
 public:
  explicit DirectoryServiceConnector(fidl::InterfaceHandle<fuchsia::io::Directory> dir)
      : dir_(std::move(dir)) {}

  zx_status_t Connect(const std::string& path, zx::channel channel) const override {
    return fdio_service_connect_at(dir_.channel().get(), path.data(), channel.release());
  }

 private:
  fidl::InterfaceHandle<fuchsia::io::Directory> dir_;
};

}  // namespace

const char kDefaultInstance[] = "default";

std::unique_ptr<fidl::ServiceConnector> OpenNamedServiceAt(
    const fidl::InterfaceHandle<fuchsia::io::Directory>& handle, const std::string& service_path,
    const std::string& instance) {
  if (service_path.compare(0, 1, "/") == 0) {
    return nullptr;
  }
  std::string path = service_path + '/' + instance;

  fidl::InterfaceHandle<fuchsia::io::Directory> dir;
  zx_status_t status = fdio_service_connect_at(handle.channel().get(), path.data(),
                                               dir.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    return nullptr;
  }
  return std::make_unique<DirectoryServiceConnector>(std::move(dir));
}

std::unique_ptr<fidl::ServiceConnector> OpenNamedServiceIn(fdio_ns_t* ns,
                                                           const std::string& service_path,
                                                           const std::string& instance) {
  std::string path;
  if (service_path.compare(0, 1, "/") != 0) {
    path = "/svc/";
  }
  path += service_path + '/' + instance;

  fidl::InterfaceHandle<fuchsia::io::Directory> dir;
  zx_status_t status = fdio_ns_connect(ns, path.data(), fuchsia::io::OPEN_RIGHT_READABLE,
                                       dir.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    return nullptr;
  }
  return std::make_unique<DirectoryServiceConnector>(std::move(dir));
}

std::unique_ptr<fidl::ServiceConnector> OpenNamedService(const std::string& service_path,
                                                         const std::string& instance) {
  fdio_ns_t* ns;
  zx_status_t status = fdio_ns_get_installed(&ns);
  if (status != ZX_OK) {
    return nullptr;
  }
  return OpenNamedServiceIn(ns, service_path, instance);
}

}  // namespace sys
