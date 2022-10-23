// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_SERVICE_HANDLER_INTERFACE_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_SERVICE_HANDLER_INTERFACE_H_

#include <lib/fidl/cpp/wire/string_view.h>
#include <lib/fit/function.h>
#include <zircon/fidl.h>

#include <string_view>

#ifdef __Fuchsia__
#include <lib/fidl/cpp/wire/internal/endpoints.h>
#include <lib/fidl/cpp/wire/internal/transport.h>
#include <lib/zx/result.h>
#endif  // __Fuchsia__

namespace fidl {

#ifdef __Fuchsia__

// Interface used by generated FIDL code for adding protocol members to a Service instance.
class ServiceHandlerInterface {
 public:
  virtual ~ServiceHandlerInterface() = default;

  // User-defined action for handling a connection attempt to a
  // member FIDL protocol defined by |Protocol|.
  // For example, if |Protocol| is spoken over Zircon channels, the handler takes a
  // |fidl::ServerEnd<Protocol>|.
  template <typename Protocol>
  using MemberHandler = fit::function<void(fidl::internal::ServerEndType<Protocol>)>;

  // Add a |member| to the instance, which will be handled by |handler|.
  //
  // This method specifies the exact protocol |Protocol|, hence should be
  // used by end-users adding service member handlers to a service directory.
  //
  // # Errors
  //
  // ZX_ERR_ALREADY_EXISTS: The member already exists.
  template <typename Protocol>
  zx::result<> AddMember(std::string_view member, MemberHandler<Protocol> handler) {
    return AddAnyMember(member,
                        [handler = std::move(handler)](fidl::internal::AnyTransport channel) {
                          return handler(::fidl::internal::ServerEndType<Protocol>(
                              channel.release<typename Protocol::Transport>()));
                        });
  }

 protected:
  // User-defined action for handling a connection attempt to any
  // member FIDL protocol.
  using AnyMemberHandler = fit::function<void(fidl::internal::AnyTransport)>;

  // Add a |member| to the instance, whose connection will be handled by |handler|.
  //
  // This variant does not restrict on the protocol type, hence should be
  // implemented by service directories (typically filesystem servers)
  // which host arbitrary member protocols under |member| paths.
  //
  // # Errors
  //
  // ZX_ERR_ALREADY_EXISTS: The member already exists.
  virtual zx::result<> AddAnyMember(std::string_view member, AnyMemberHandler handler) = 0;
};

#endif  // __Fuchsia__

}  // namespace fidl

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_SERVICE_HANDLER_INTERFACE_H_
