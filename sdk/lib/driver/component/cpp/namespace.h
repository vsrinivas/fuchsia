// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_COMPONENT_CPP_NAMESPACE_H_
#define LIB_DRIVER_COMPONENT_CPP_NAMESPACE_H_

#include <fidl/fuchsia.component.runner/cpp/fidl.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/driver/component/cpp/handlers.h>
#include <lib/fdf/cpp/protocol.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/namespace.h>
#include <lib/fidl_driver/cpp/transport.h>
#include <lib/sys/component/cpp/service_client.h>

namespace driver {

namespace internal {

template <typename T>
static constexpr std::false_type always_false{};

// Returns a client_end to the connection.
template <typename ServiceMember>
zx::result<fdf::ClientEnd<typename ServiceMember::ProtocolType>> DriverTransportConnect(
    fidl::UnownedClientEnd<fuchsia_io::Directory> svc_dir, std::string_view instance) {
  static_assert((std::is_same_v<typename ServiceMember::ProtocolType::Transport,
                                fidl::internal::DriverTransport>),
                "ServiceMember must use DriverTransport. Double check the FIDL protocol.");

  zx::channel client_token, server_token;
  auto status = zx::channel::create(0, &client_token, &server_token);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  status = fdio_open_at(
      svc_dir.handle()->get(), component::MakeServiceMemberPath<ServiceMember>(instance).c_str(),
      static_cast<uint32_t>(fuchsia_io::wire::OpenFlags::kRightReadable), server_token.release());

  if (status != ZX_OK) {
    return zx::error(status);
  }

  auto endpoints = fdf::CreateEndpoints<typename ServiceMember::ProtocolType>();
  if (endpoints.is_error()) {
    return endpoints.take_error();
  }

  status = fdf::ProtocolConnect(std::move(client_token), std::move(endpoints->server.TakeHandle()));
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(std::move(endpoints->client));
}

// Uses the passed in server_end to make the connection.
template <typename ServiceMember>
zx::result<> DriverTransportConnect(fidl::UnownedClientEnd<fuchsia_io::Directory> svc_dir,
                                    fdf::ServerEnd<typename ServiceMember::ProtocolType> server_end,
                                    std::string_view instance) {
  static_assert((std::is_same_v<typename ServiceMember::ProtocolType::Transport,
                                fidl::internal::DriverTransport>),
                "ServiceMember must use DriverTransport. Double check the FIDL protocol.");

  zx::channel client_token, server_token;
  auto status = zx::channel::create(0, &client_token, &server_token);
  if (status != ZX_OK) {
    return zx::error(status);
  }

  status = fdio_open_at(
      svc_dir.handle()->get(), component::MakeServiceMemberPath<ServiceMember>(instance).c_str(),
      static_cast<uint32_t>(fuchsia_io::wire::OpenFlags::kRightReadable), server_token.release());

  if (status != ZX_OK) {
    return zx::error(status);
  }

  status = fdf::ProtocolConnect(std::move(client_token), std::move(server_end.TakeHandle()));
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok();
}
}  // namespace internal

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
  // DriverTransport is not supported. Protocols using DriverTransport must be service members.
  template <typename Protocol, typename = std::enable_if_t<!fidl::IsServiceMemberV<Protocol>>>
  zx::result<fidl::ClientEnd<Protocol>> Connect(
      const char* protocol_name = fidl::DiscoverableProtocolName<Protocol>) const {
    static_assert((std::is_same_v<typename Protocol::Transport, fidl::internal::ChannelTransport>),
                  "Protocol must use ChannelTransport. Use a ServiceMember for DriverTransport.");
    return component::ConnectAt<Protocol>(svc_dir(), protocol_name);
  }

  // Connects |server_end| to a protocol within a driver's namespace.
  // DriverTransport is not supported. Protocols using DriverTransport must be service members.
  template <typename Protocol, typename = std::enable_if_t<!fidl::IsServiceMemberV<Protocol>>>
  zx::result<> Connect(fidl::ServerEnd<Protocol> server_end,
                       const char* protocol_name = fidl::DiscoverableProtocolName<Protocol>) const {
    static_assert((std::is_same_v<typename Protocol::Transport, fidl::internal::ChannelTransport>),
                  "Protocol must use ChannelTransport. Use a ServiceMember for DriverTransport.");
    return component::ConnectAt(svc_dir(), std::move(server_end), protocol_name);
  }

  // Connect to a service within a driver's namespace.
  template <typename FidlService>
  zx::result<typename FidlService::ServiceClient> OpenService(std::string_view instance) const {
    static_assert(fidl::IsServiceV<FidlService>, "FidlService must be a service.");
    return component::OpenServiceAt<FidlService>(svc_dir(), instance);
  }

  // Protocol must compose fuchsia.io/Node.
  // DriverTransport is not supported. Protocols using DriverTransport must be service members.
  template <typename Protocol>
  zx::result<fidl::ClientEnd<Protocol>> Open(const char* path,
                                             fuchsia_io::wire::OpenFlags flags) const {
    static_assert(!fidl::IsServiceMemberV<Protocol>, "Protocol must not be a ServiceMember.");
    static_assert((std::is_same_v<typename Protocol::Transport, fidl::internal::ChannelTransport>),
                  "Protocol must use ChannelTransport. Use a ServiceMember for DriverTransport.");
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

  // Opens the |path| in the driver's namespace.
  // DriverTransport is not supported.
  zx::result<> Open(const char* path, fuchsia_io::wire::OpenFlags flags,
                    zx::channel server_end) const;

  // Connects to the |ServiceMember| protocol.
  //
  // |instance| refers to the name of the instance of the service.
  //
  // Returns a ClientEnd of type corresponding to the given protocol
  // e.g. fidl::ClientEnd or fdf::ClientEnd.
  template <typename ServiceMember>
  zx::result<fidl::internal::ClientEndType<typename ServiceMember::ProtocolType>> Connect(
      std::string_view instance = component::kDefaultInstance) const {
    static_assert(
        fidl::IsServiceMemberV<ServiceMember>,
        "ServiceMember type must be the Protocol inside of a Service, eg: fuchsia_hardware_pci::Service::Device.");
    if constexpr (std::is_same_v<typename ServiceMember::ProtocolType::Transport,
                                 fidl::internal::ChannelTransport>) {
      return component::ConnectAt<ServiceMember>(svc_dir(), instance);
    } else if constexpr (std::is_same_v<typename ServiceMember::ProtocolType::Transport,
                                        fidl::internal::DriverTransport>) {
      return internal::DriverTransportConnect<ServiceMember>(svc_dir(), instance);
    } else {
      static_assert(internal::always_false<ServiceMember>);
    }
  }

  // Connects |server_end| to the |ServiceMember| protocol.
  //
  // |instance| refers to the name of the instance of the service.
  //
  // The type of |server_end| must correspond to the given protocol's transport
  // e.g. fidl::ServerEnd for ChannelTransport or fdf::ServerEnd for DriverTransport.
  template <typename ServiceMember>
  zx::result<> Connect(
      fidl::internal::ServerEndType<typename ServiceMember::ProtocolType> server_end,
      std::string_view instance = component::kDefaultInstance) const {
    static_assert(
        fidl::IsServiceMemberV<ServiceMember>,
        "ServiceMember type must be the Protocol inside of a Service, eg: fuchsia_hardware_pci::Service::Device.");
    if constexpr (std::is_same_v<typename ServiceMember::ProtocolType::Transport,
                                 fidl::internal::ChannelTransport>) {
      return component::ConnectAt<ServiceMember>(svc_dir(), std::move(server_end), instance);
    } else if constexpr (std::is_same_v<typename ServiceMember::ProtocolType::Transport,
                                        fidl::internal::DriverTransport>) {
      return internal::DriverTransportConnect<ServiceMember>(svc_dir(), std::move(server_end),
                                                             instance);
    } else {
      static_assert(internal::always_false<ServiceMember>);
    }
  }

  fidl::UnownedClientEnd<fuchsia_io::Directory> svc_dir() const { return svc_dir_; }

 private:
  explicit Namespace(fdio_ns_t* ns, fidl::ClientEnd<fuchsia_io::Directory> svc_dir);

  Namespace(const Namespace& other) = delete;
  Namespace& operator=(const Namespace& other) = delete;

  fdio_ns_t* ns_ = nullptr;
  fidl::ClientEnd<fuchsia_io::Directory> svc_dir_;
};

}  // namespace driver

#endif  // LIB_DRIVER_COMPONENT_CPP_NAMESPACE_H_
