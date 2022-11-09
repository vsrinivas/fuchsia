// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_SERVICE_CPP_SERVICE_AGGREGATE_H_
#define LIB_SYS_SERVICE_CPP_SERVICE_AGGREGATE_H_

#include <fuchsia/io/cpp/fidl.h>
#include <lib/fdio/namespace.h>
#include <lib/zx/channel.h>

namespace sys {

// A base class for a service aggregate, providing common functionality.
class ServiceAggregateBase {
 public:
  // Returns whether the underlying directory is valid.
  bool is_valid() const { return dir_.is_bound(); }

  // Returns the underlying directory.
  const fidl::SynchronousInterfacePtr<fuchsia::io::Directory>& proxy() const { return dir_; }

  // Lists all available instances of a service.
  std::vector<std::string> ListInstances() const;

 protected:
  explicit ServiceAggregateBase(fidl::InterfaceHandle<fuchsia::io::Directory> dir)
      : dir_(dir.BindSync()) {}

 private:
  const fidl::SynchronousInterfacePtr<fuchsia::io::Directory> dir_;
};

// A service aggregate, containing zero or more instances of a service.
template <typename Service>
struct ServiceAggregate final : public ServiceAggregateBase {
  // Constructs a service aggregate from a `fuchsia::io::Directory`.
  explicit ServiceAggregate(fidl::InterfaceHandle<fuchsia::io::Directory> dir)
      : ServiceAggregateBase(std::move(dir)) {}
};

// Opens a service aggregate at |service_path|, within a directory provided by
// |handle|.
//
// A service aggregate contains zero or more instances of a service.
//
// Returns a `fuchsia::io::Directory`, representing a service aggregate.
fidl::InterfaceHandle<fuchsia::io::Directory> OpenNamedServiceAggregateAt(
    const fidl::InterfaceHandle<fuchsia::io::Directory>& handle, const std::string& service_path);

// Opens a service aggregate for |Service|, within a directory provided by
// |handle|.
//
// A service aggregate contains zero or more instances of a service.
//
// Returns a |ServiceAggregate| for a FIDL-generated service.
template <typename Service>
ServiceAggregate<Service> OpenServiceAggregateAt(
    const fidl::InterfaceHandle<fuchsia::io::Directory>& handle) {
  return ServiceAggregate<Service>(OpenNamedServiceAggregateAt(handle, Service::Name));
}

// Opens a service aggregate at |service_path|, within a namespace provided by
// |ns|.
//
// A service aggregate contains zero or more instances of a service.
//
// |ns| must not be null.
//
// Returns a `fuchsia::io::Directory`, representing a service aggregate.
fidl::InterfaceHandle<fuchsia::io::Directory> OpenNamedServiceAggregateIn(
    fdio_ns_t* ns, const std::string& service_path);

// Opens a service aggregate for |Service|, within a namespace provided by |ns|.
//
// A service aggregate contains zero or more instances of a service.
//
// |ns| must not be null.
//
// Returns a |ServiceAggregate| for a FIDL-generated service.
template <typename Service>
ServiceAggregate<Service> OpenServiceAggregateIn(fdio_ns_t* ns) {
  return ServiceAggregate<Service>(OpenNamedServiceAggregateIn(ns, Service::Name));
}

// Opens a service aggregate at |service_path|, within the default namespace.
//
// A service aggregate contains zero or more instances of a service.
//
// See `fdio_ns_get_installed()`.
//
// Returns a `fuchsia::io::Directory`, representing a service aggregate.
fidl::InterfaceHandle<fuchsia::io::Directory> OpenNamedServiceAggregate(
    const std::string& service_path);

// Opens a service aggregate for |Service|, within the default namespace.
//
// A service aggregate contains zero or more instances of a service.
//
// See `fdio_ns_get_installed()`.
//
// Returns a |ServiceAggregate| for a FIDL-generated service.
template <typename Service>
ServiceAggregate<Service> OpenServiceAggregate() {
  return ServiceAggregate<Service>(OpenNamedServiceAggregate(Service::Name));
}

}  // namespace sys

#endif  // LIB_SYS_SERVICE_CPP_SERVICE_AGGREGATE_H_
