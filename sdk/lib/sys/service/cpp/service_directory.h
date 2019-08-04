// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_SERVICE_CPP_SERVICE_DIRECTORY_H_
#define LIB_SYS_SERVICE_CPP_SERVICE_DIRECTORY_H_

#include <fuchsia/io/cpp/fidl.h>
#include <lib/fdio/namespace.h>
#include <lib/zx/channel.h>

namespace fidl {

// A base class for a service directory, providing common functionality.
class ServiceDirectoryBase {
 public:
  // Returns whether the underlying directory is valid.
  bool is_valid() const { return dir_.is_valid(); }

  // Returns the channel of the underlying directory.
  const zx::channel& channel() const { return dir_.channel(); }

  // Lists all available instances of a service.
  std::vector<std::string> ListInstances() const;

 protected:
  explicit ServiceDirectoryBase(InterfaceHandle<fuchsia::io::Directory> dir)
      : dir_(std::move(dir)) {}

 private:
  const InterfaceHandle<fuchsia::io::Directory> dir_;
};

// A service directory, containing zero or more instances of a service.
template <typename Service>
struct ServiceDirectory final : public ServiceDirectoryBase {
  // Constructs a service directory from a `fuchsia::io::Directory`.
  explicit ServiceDirectory(InterfaceHandle<fuchsia::io::Directory> dir)
      : ServiceDirectoryBase(std::move(dir)) {}
};

// Opens a service directory at |service_path|, within a directory provided by
// |handle|.
//
// A service directory contains zero or more instances of a service.
//
// Returns a `fuchsia::io::Directory`, representing a service directory.
InterfaceHandle<fuchsia::io::Directory> OpenNamedServiceDirectoryAt(
    const InterfaceHandle<fuchsia::io::Directory>& handle, const std::string& service_path);

// Opens a service directory for |Service|, within a directory provided by
// |handle|.
//
// A service directory contains zero or more instances of a service.
//
// Returns a |ServiceDirectory| for a FIDL-generated service.
template <typename Service>
ServiceDirectory<Service> OpenServiceDirectoryAt(
    const InterfaceHandle<fuchsia::io::Directory>& handle) {
  return ServiceDirectory<Service>(OpenNamedServiceDirectoryAt(handle, Service::Name));
}

// Opens a service directory at |service_path|, within a namespace provided by
// |ns|.
//
// A service directory contains zero or more instances of a service.
//
// |ns| must not be null.
//
// Returns a `fuchsia::io::Directory`, representing a service directory.
InterfaceHandle<fuchsia::io::Directory> OpenNamedServiceDirectoryIn(
    fdio_ns_t* ns, const std::string& service_path);

// Opens a service directory for |Service|, within a namespace provided by |ns|.
//
// A service directory contains zero or more instances of a service.
//
// |ns| must not be null.
//
// Returns a |ServiceDirectory| for a FIDL-generated service.
template <typename Service>
ServiceDirectory<Service> OpenServiceDirectoryIn(fdio_ns_t* ns) {
  return ServiceDirectory<Service>(OpenNamedServiceDirectoryIn(ns, Service::Name));
}

// Opens a service directory at |service_path|, within the default namespace.
//
// A service directory contains zero or more instances of a service.
//
// See `fdio_ns_get_installed()`.
//
// Returns a `fuchsia::io::Directory`, representing a service directory.
InterfaceHandle<fuchsia::io::Directory> OpenNamedServiceDirectory(const std::string& service_path);

// Opens a service directory for |Service|, within the default namespace.
//
// A service directory contains zero or more instances of a service.
//
// See `fdio_ns_get_installed()`.
//
// Returns a |ServiceDirectory| for a FIDL-generated service.
template <typename Service>
ServiceDirectory<Service> OpenServiceDirectory() {
  return ServiceDirectory<Service>(OpenNamedServiceDirectory(Service::Name));
}

}  // namespace fidl

#endif  // LIB_SYS_SERVICE_CPP_SERVICE_DIRECTORY_H_
