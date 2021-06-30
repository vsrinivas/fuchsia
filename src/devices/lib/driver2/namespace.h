// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_DRIVER2_NAMESPACE_H_
#define SRC_DEVICES_LIB_DRIVER2_NAMESPACE_H_

#include <fuchsia/component/runner/llcpp/fidl.h>
#include <lib/fdio/namespace.h>
#include <lib/zx/status.h>

// Manages a driver's namespace.
class Namespace {
 public:
  // Creates a namespace from `DriverStartArgs::ns`.
  static zx::status<Namespace> Create(
      fidl::VectorView<fuchsia_component_runner::wire::ComponentNamespaceEntry>& entries);

  Namespace() = default;
  ~Namespace();

  Namespace(Namespace&& other) noexcept;
  Namespace& operator=(Namespace&& other) noexcept;

  // Connect to a service within a driver's namespace.
  template <typename T>
  zx::status<fidl::ClientEnd<T>> Connect(
      std::string_view path = fidl::DiscoverableProtocolDefaultPath<T>) const {
    auto endpoints = fidl::CreateEndpoints<T>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }
    auto result = Connect(path, endpoints->server.TakeChannel());
    if (result.is_error()) {
      return result.take_error();
    }
    return zx::ok(std::move(endpoints->client));
  }

 private:
  explicit Namespace(fdio_ns_t* ns);

  Namespace(const Namespace& other) = delete;
  Namespace& operator=(const Namespace& other) = delete;

  zx::status<> Connect(std::string_view path, zx::channel server_end) const;

  fdio_ns_t* ns_ = nullptr;
};

#endif  // SRC_DEVICES_LIB_DRIVER2_NAMESPACE_H_
