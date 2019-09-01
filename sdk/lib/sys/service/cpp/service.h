// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_SERVICE_CPP_SERVICE_H_
#define LIB_SYS_SERVICE_CPP_SERVICE_H_

#include <fuchsia/io/cpp/fidl.h>
#include <lib/fdio/namespace.h>
#include <lib/fidl/cpp/service_connector.h>

namespace sys {

// Name of the default instance of a service.
extern const char kDefaultInstance[];

// Opens a named |instance| of a service at |service_path|, within a directory
// provided by |handle|.
//
// The instance is opened under `service_path/instance` within |handle|.
//
// If |instance| is not provided, the default instance is opened.
//
// Returns a `fuchsia::io::Directory`, representing an instance of the service.
std::unique_ptr<fidl::ServiceConnector> OpenNamedServiceAt(
    const fidl::InterfaceHandle<fuchsia::io::Directory>& handle, const std::string& service_path,
    const std::string& instance = kDefaultInstance);

// Opens a named |instance| of a |Service|, within a directory provided by
// |handle|.
//
// The instance is opened under `Service::Name/instance` within |handle|.
//
// If |instance| is not provided, the default instance is opened.
//
// Returns a |Service|, which is a FIDL-generated service.
template <typename Service>
Service OpenServiceAt(const fidl::InterfaceHandle<fuchsia::io::Directory>& handle,
                      const std::string& instance = kDefaultInstance) {
  return Service(OpenNamedServiceAt(handle, Service::Name, instance));
}

// Opens a named |instance| of a service at |service_path|, within a namespace
// provided by |ns|.
//
// If |service_path| is an absolute path, the instance is opened under
// `service_path/instance` within |ns|. Otherwise, if |service_path| is a
// relative path, the instance is opened under `/svc/service_path/instance`
// within |ns|.
//
// If |instance| is not provided, the default instance is opened.
//
// |ns| must not be null.
//
// Returns a `fuchsia::io::Directory`, representing an instance of the service.
std::unique_ptr<fidl::ServiceConnector> OpenNamedServiceIn(
    fdio_ns_t* ns, const std::string& service_path, const std::string& instance = kDefaultInstance);

// Opens a named |instance| of a |Service|, within a namespace provided by
// |ns|.
//
// The instance is opened under `/svc/Service::Name/instance` within |ns|.
//
// If |instance| is not provided, the default instance is opened.
//
// |ns| must not be null.
//
// Returns a |Service|, which is a FIDL-generated service.
template <typename Service>
Service OpenServiceIn(fdio_ns_t* ns, const std::string& instance = kDefaultInstance) {
  return Service(OpenNamedServiceIn(ns, Service::Name, instance));
}

// Opens a named |instance| of a service at |service_path|, within the default
// namespace.
//
// If |service_path| is an absolute path, the instance is opened under
// `service_path/instance` within the default namespace. Otherwise, if
// |service_path| is a relative path, the instance is opened under
// `/svc/service_path/instance` within the default namespace.
//
// If |instance| is not provided, the default instance is opened.
//
// See `fdio_ns_get_installed()`.
//
// Returns a `fuchsia::io::Directory`, representing an instance of the service.
std::unique_ptr<fidl::ServiceConnector> OpenNamedService(
    const std::string& service_path, const std::string& instance = kDefaultInstance);

// Opens a named |instance| of a |Service|, within the default namespace.
//
// The instance is opened under `/svc/Service::Name/instance` within the default
// namespace.
//
// If |instance| is not provided, the default instance is opened.
//
// See `fdio_ns_get_installed()`.
//
// Returns a |Service|, which is a FIDL-generated service.
template <typename Service>
Service OpenService(const std::string& instance = kDefaultInstance) {
  return Service(OpenNamedService(Service::Name, instance));
}

}  // namespace sys

#endif  // LIB_SYS_SERVICE_CPP_SERVICE_H_
