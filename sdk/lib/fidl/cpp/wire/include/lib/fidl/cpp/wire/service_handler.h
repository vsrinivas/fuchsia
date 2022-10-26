// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_SERVICE_HANDLER_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_SERVICE_HANDLER_H_

#include <lib/fidl/cpp/wire/string_view.h>
#include <lib/fit/function.h>
#include <zircon/fidl.h>

#include <map>
#include <string>
#include <string_view>

#ifdef __Fuchsia__
#include <lib/fidl/cpp/wire/internal/endpoints.h>
#include <lib/fidl/cpp/wire/internal/transport.h>
#include <lib/zx/result.h>
#endif  // __Fuchsia__

namespace fidl {

#ifdef __Fuchsia__

// Class used by generated FIDL code for adding protocol members to a Service instance.
template <class Transport>
class ServiceInstanceHandler {
 public:
  using TransportType = typename Transport::OwnedType;

  ServiceInstanceHandler() = default;
  ~ServiceInstanceHandler() = default;

  // Disallow copying.
  ServiceInstanceHandler(const ServiceInstanceHandler&) = delete;
  ServiceInstanceHandler& operator=(const ServiceInstanceHandler&) = delete;

  // Enable moving.
  ServiceInstanceHandler(ServiceInstanceHandler&&) = default;
  ServiceInstanceHandler& operator=(ServiceInstanceHandler&&) = default;

  // User-defined action for handling a connection attempt to a
  // member FIDL protocol defined by |Protocol|.
  // For example, if |Protocol| is spoken over Zircon channels, the handler takes a
  // |fidl::ServerEnd<Protocol>|.
  template <typename Protocol>
  using MemberHandler = fit::function<void(fidl::internal::ServerEndType<Protocol>)>;

  // User-defined action for handling a connection attempt to any
  // member FIDL protocol.
  using AnyMemberHandler = fit::function<void(TransportType)>;

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
    std::string owned_member = std::string(member);
    if (handlers_.count(owned_member) != 0) {
      return zx::make_result(ZX_ERR_ALREADY_EXISTS);
    }

    auto bridge_func = [handler = std::move(handler)](TransportType channel) {
      return handler(fidl::internal::ServerEndType<Protocol>(std::move(channel)));
    };
    handlers_[owned_member] = std::move(bridge_func);
    return zx::ok();
  }

  // Return all registered member handlers. Key contains member name. Value
  // contains connector functions.
  //
  // Once taken, the `ServiceInstanceHandler` is no longer safe to use.
  std::map<std::string, AnyMemberHandler> TakeMemberHandlers() { return std::move(handlers_); }

 private:
  std::map<std::string, AnyMemberHandler> handlers_ = {};
};

#endif  // __Fuchsia__

}  // namespace fidl

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_SERVICE_HANDLER_H_
