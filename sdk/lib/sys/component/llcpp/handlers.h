// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_COMPONENT_LLCPP_HANDLERS_H_
#define LIB_SYS_COMPONENT_LLCPP_HANDLERS_H_

#include <lib/fidl/llcpp/service_handler_interface.h>
#include <lib/svc/dir.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>
#include <zircon/errors.h>

#include <map>

namespace component_llcpp {

// Callback invoked when a request is made to a FIDL protocol server end.
using AnyHandler = fit::function<void(zx::channel)>;

// Same as |AnyHandler| except that it's typesafe.
template <typename Protocol>
using TypedHandler = fit::function<void(fidl::ServerEnd<Protocol> request)>;

// A handler for an instance of a FIDL Service.
class ServiceHandler final : public fidl::ServiceHandlerInterface {
 public:
  ServiceHandler() = default;

  // Disable copying.
  ServiceHandler(const ServiceHandler&) = delete;
  ServiceHandler& operator=(const ServiceHandler&) = delete;

  // Enable moving.
  ServiceHandler(ServiceHandler&&) = default;
  ServiceHandler& operator=(ServiceHandler&&) = default;

 private:
  friend class OutgoingDirectory;

  // Return all registered member handlers. Key contains member name. Value
  // contains |Connector| func.
  //
  // Once taken, the service handler is no longer safe to use.
  std::map<std::string, AnyHandler> GetMemberHandlers() { return std::move(handlers_); }

  // Add a |member| to the instance, whose connection will be handled by |handler|.
  //
  // # Errors
  //
  // ZX_ERR_ALREADY_EXISTS: The member already exists.
  zx::status<> AddAnyMember(cpp17::string_view member, AnyMemberHandler handler) override {
    std::string owned_member = std::string(member);
    if (handlers_.count(owned_member) != 0) {
      return zx::make_status(ZX_ERR_ALREADY_EXISTS);
    }

    // Since AnyMemberHandler is a protected type of this class' parent class,
    // |fidl::ServiceHandlerInterface|, wrap the type into a public one.
    AnyHandler bridge_func = [handler = std::move(handler)](zx::channel request_channel) {
      // We scrub the return value of |handler| which is a |zx::status| because
      // it is unused by the internal VFS implementation.
      // TODO(https://fxbug.dev/92898): Remove the return type from |AnyMemberHandler|
      // and |MemberHandler| in |ServiceHandlerInterface|.
      (void)handler(std::move(request_channel));
    };
    handlers_[owned_member] = std::move(bridge_func);
    return zx::ok();
  }

  std::map<std::string, AnyHandler> handlers_ = {};
};

}  // namespace component_llcpp

#endif  // LIB_SYS_COMPONENT_LLCPP_HANDLERS_H_
