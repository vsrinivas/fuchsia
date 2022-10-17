// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER2_SERVICE_CLIENT_H_
#define LIB_DRIVER2_SERVICE_CLIENT_H_

#include <lib/driver2/handlers.h>
#include <lib/driver2/namespace.h>
#include <lib/driver2/outgoing_directory.h>
#include <lib/fdf/cpp/protocol.h>

namespace driver {

namespace internal {

template <typename T>
static constexpr std::false_type always_false{};

template <typename ServiceMember>
std::string MakeServiceMemberPath(std::string_view instance) {
  return std::string(ServiceMember::ServiceName)
      .append("/")
      .append(instance)
      .append("/")
      .append(ServiceMember::Name);
}

template <typename ServiceMember>
zx::result<fdf::ClientEnd<typename ServiceMember::ProtocolType>> DriverTransportConnect(
    const driver::Namespace& ns, std::string_view instance = kDefaultInstance) {
  ZX_ASSERT((std::is_same_v<typename ServiceMember::ProtocolType::Transport,
                            fidl::internal::DriverTransport>));

  zx::channel client_token, server_token;
  auto status = zx::channel::create(0, &client_token, &server_token);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  auto path = "/svc/" + MakeServiceMemberPath<ServiceMember>(instance);
  auto result =
      ns.Open(path.c_str(), fuchsia_io::wire::OpenFlags::kRightReadable, std::move(server_token));
  if (result.is_error()) {
    return result.take_error();
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

}  // namespace internal

// Connects to the |ServiceMember| protocol in the namespace |ns|.
//
// |instance| refers to the name of the instance of the service.
//
// Returns a ClientEnd of type corresponding to the given protocol
// e.g. fidl::ClientEnd or fdf::ClientEnd.
template <typename ServiceMember,
          typename = std::enable_if_t<fidl::IsServiceMemberV<ServiceMember>>>
zx::result<fidl::internal::ClientEndType<typename ServiceMember::ProtocolType>> Connect(
    const driver::Namespace& ns, std::string_view instance = kDefaultInstance) {
  if constexpr (std::is_same_v<typename ServiceMember::ProtocolType::Transport,
                               fidl::internal::ChannelTransport>) {
    return component::ConnectAt<ServiceMember>(ns.svc_dir(), instance);
  } else if constexpr (std::is_same_v<typename ServiceMember::ProtocolType::Transport,
                                      fidl::internal::DriverTransport>) {
    return internal::DriverTransportConnect<ServiceMember>(ns, instance);
  } else {
    static_assert(internal::always_false<ServiceMember>);
  }
}

}  // namespace driver

#endif  // LIB_DRIVER2_SERVICE_CLIENT_H_
