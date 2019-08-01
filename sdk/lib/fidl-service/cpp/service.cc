// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/fidl-service/cpp/service.h>

namespace fidl {

const char kDefaultInstance[] = "default";

InterfaceHandle<fuchsia::io::Directory> OpenNamedServiceAt(
    const InterfaceHandle<fuchsia::io::Directory>& handle, const std::string& service_path,
    const std::string& instance) {
  if (service_path.compare(0, 1, "/") == 0) {
    return nullptr;
  }
  std::string path = service_path + '/' + instance;

  InterfaceHandle<fuchsia::io::Directory> dir;
  zx_status_t status = fdio_service_connect_at(handle.channel().get(), path.data(),
                                               dir.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    return nullptr;
  }
  return dir;
}

InterfaceHandle<fuchsia::io::Directory> OpenNamedServiceIn(fdio_ns_t* ns,
                                                           const std::string& service_path,
                                                           const std::string& instance) {
  std::string path;
  if (service_path.compare(0, 1, "/") != 0) {
    path = "/svc/";
  }
  path += service_path + '/' + instance;

  InterfaceHandle<fuchsia::io::Directory> dir;
  zx_status_t status = fdio_ns_connect(ns, path.data(), fuchsia::io::OPEN_RIGHT_READABLE,
                                       dir.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    return nullptr;
  }
  return dir;
}

InterfaceHandle<fuchsia::io::Directory> OpenNamedService(const std::string& service_path,
                                                         const std::string& instance) {
  fdio_ns_t* ns;
  zx_status_t status = fdio_ns_get_installed(&ns);
  if (status != ZX_OK) {
    return nullptr;
  }
  return OpenNamedServiceIn(ns, service_path, instance);
}

}  // namespace fidl
