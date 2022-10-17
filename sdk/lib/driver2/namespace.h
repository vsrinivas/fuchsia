// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER2_NAMESPACE_H_
#define LIB_DRIVER2_NAMESPACE_H_

#include <fidl/fuchsia.component.runner/cpp/fidl.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/namespace.h>
#include <lib/sys/component/cpp/service_client.h>

namespace driver {

// Manages a driver's namespace.
class Namespace {
 public:
  // Creates a namespace from `DriverStartArgs::ns`.
  static zx::result<Namespace> Create(
      fidl::VectorView<fuchsia_component_runner::wire::ComponentNamespaceEntry>& entries);

  // Creates a namespace from natural types version of `DriverStartArgs::ns`.
  static zx::result<Namespace> Create(
      std::vector<fuchsia_component_runner::ComponentNamespaceEntry>& entries);

  Namespace() = default;
  ~Namespace();

  Namespace(Namespace&& other) noexcept;
  Namespace& operator=(Namespace&& other) noexcept;

  // Connect to a protocol within a driver's namespace.
  template <typename Protocol>
  zx::result<fidl::ClientEnd<Protocol>> Connect(
      const char* protocol_name = fidl::DiscoverableProtocolName<Protocol>) const {
    return component::ConnectAt<Protocol>(svc_dir(), protocol_name);
  }

  template <typename Protocol>
  zx::result<> Connect(fidl::ServerEnd<Protocol> server_end,
                       const char* protocol_name = fidl::DiscoverableProtocolName<Protocol>) const {
    return component::ConnectAt(svc_dir(), std::move(server_end), protocol_name);
  }

  // Connect to a service within a driver's namespace.
  template <typename FidlService>
  zx::result<typename FidlService::ServiceClient> OpenService(std::string_view instance) const {
    return component::OpenServiceAt<FidlService>(svc_dir(), instance);
  }

  // Protocol must compose fuchsia.io/Node.
  template <typename Protocol>
  zx::result<fidl::ClientEnd<Protocol>> Open(const char* path,
                                             fuchsia_io::wire::OpenFlags flags) const {
    auto endpoints = fidl::CreateEndpoints<Protocol>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }
    zx::result result = Open(path, flags, endpoints->server.TakeChannel());
    if (result.is_error()) {
      return result.take_error();
    }
    return zx::ok(std::move(endpoints->client));
  }
  zx::result<> Open(const char* path, fuchsia_io::wire::OpenFlags flags,
                    zx::channel server_end) const;

  fidl::UnownedClientEnd<fuchsia_io::Directory> svc_dir() const { return svc_dir_; }

 private:
  explicit Namespace(fdio_ns_t* ns, fidl::ClientEnd<fuchsia_io::Directory> svc_dir);

  Namespace(const Namespace& other) = delete;
  Namespace& operator=(const Namespace& other) = delete;

  fdio_ns_t* ns_ = nullptr;
  fidl::ClientEnd<fuchsia_io::Directory> svc_dir_;
};

}  // namespace driver

#endif  // LIB_DRIVER2_NAMESPACE_H_
