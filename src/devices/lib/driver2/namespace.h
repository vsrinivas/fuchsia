// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_DRIVER2_NAMESPACE_H_
#define SRC_DEVICES_LIB_DRIVER2_NAMESPACE_H_

#include <fidl/fuchsia.component.runner/cpp/wire.h>
#include <lib/fdio/namespace.h>

namespace driver {

namespace internal {

zx::status<> DirectoryOpenFunc(zx::unowned_channel dir, fidl::StringView path, zx::channel remote);

}  // namespace internal

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

  // Connect to a protocol within a driver's namespace.
  template <typename T>
  zx::status<fidl::ClientEnd<T>> Connect(
      std::string_view path = fidl::DiscoverableProtocolDefaultPath<T>,
      fuchsia_io::wire::OpenFlags flags = fuchsia_io::wire::kOpenRightReadable) const {
    auto endpoints = fidl::CreateEndpoints<T>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }
    auto result = Connect(path, endpoints->server.TakeChannel(), flags);
    if (result.is_error()) {
      return result.take_error();
    }
    return zx::ok(std::move(endpoints->client));
  }

  // Connect to a service within a driver's namespace.
  template <typename FidlService>
  zx::status<typename FidlService::ServiceClient> OpenService(cpp17::string_view instance) const {
    std::string path = std::string("/") + FidlService::Name + "/" + std::string(instance);
    auto result = Connect<fuchsia_io::Directory>(
        path, fuchsia_io::wire::kOpenRightReadable | fuchsia_io::wire::kOpenRightWritable);
    if (result.is_error()) {
      return result.take_error();
    }

    return zx::ok(typename FidlService::ServiceClient(std::move(result.value().TakeChannel()),
                                                      internal::DirectoryOpenFunc));
  }

  zx::status<> Connect(
      std::string_view path, zx::channel server_end,
      fuchsia_io::wire::OpenFlags flags = fuchsia_io::wire::kOpenRightReadable |
                                          fuchsia_io::wire::kOpenRightWritable) const;

 private:
  explicit Namespace(fdio_ns_t* ns);

  Namespace(const Namespace& other) = delete;
  Namespace& operator=(const Namespace& other) = delete;

  fdio_ns_t* ns_ = nullptr;
};

}  // namespace driver

#endif  // SRC_DEVICES_LIB_DRIVER2_NAMESPACE_H_
