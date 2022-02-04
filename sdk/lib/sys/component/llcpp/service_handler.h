// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_COMPONENT_LLCPP_SERVICE_HANDLER_H_
#define LIB_SYS_COMPONENT_LLCPP_SERVICE_HANDLER_H_

#include <lib/fidl/llcpp/service_handler_interface.h>
#include <lib/svc/dir.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>
#include <zircon/errors.h>

#include <map>

namespace component_llcpp {

// A handler for an instance of a FIDL Service.
class ServiceHandler final : public fidl::ServiceHandlerInterface {
 public:
  // Untyped connector to a FIDL Protocol.
  using Connector = fit::function<void(zx::channel)>;

  ServiceHandler() = default;

  // Disable copying.
  ServiceHandler(const ServiceHandler&) = delete;
  ServiceHandler& operator=(const ServiceHandler&) = delete;

  // Enable moving.
  ServiceHandler(ServiceHandler&&) = default;
  ServiceHandler& operator=(ServiceHandler&&) = default;

  // Return all registered member connectors. Key contains member name. Value
  // contains |Connector| func.
  //
  // Once taken, the service handler is no longer safe to use.
  std::map<std::string, Connector> GetMemberConnectors() { return std::move(connectors_); }

 private:
  // Add a |member| to the instance, whose connection will be handled by |handler|.
  //
  // # Errors
  //
  // ZX_ERR_ALREADY_EXISTS: The member already exists.
  zx::status<> AddAnyMember(cpp17::string_view member, AnyMemberHandler handler) override {
    std::string owned_member = std::string(member);
    if (connectors_.count(owned_member) != 0) {
      return zx::make_status(ZX_ERR_ALREADY_EXISTS);
    }

    // Since AnyMemberHandler is a protected type of this class' parent class,
    // |fidl::ServiceHandlerInterface|, wrap the type into a public one.
    Connector bridge_func = [handler = std::move(handler)](zx::channel request_channel) {
      // We scrub the return value of |handler| which is a |zx::status| because
      // it is unused by the internal VFS implementation.
      // TODO(https://fxbug.dev/92898): Remove the return type from |AnyMemberHandler|
      // and |MemberHandler| in |ServiceHandlerInterface|.
      (void)handler(std::move(request_channel));
    };
    connectors_[owned_member] = std::move(bridge_func);
    return zx::ok();
  }

  std::map<std::string, Connector> connectors_ = {};
};

}  // namespace component_llcpp

#endif  // LIB_SYS_COMPONENT_LLCPP_SERVICE_HANDLER_H_
