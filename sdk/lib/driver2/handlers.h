// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER2_HANDLERS_H_
#define LIB_DRIVER2_HANDLERS_H_

#include <lib/fdf/cpp/channel.h>
#include <lib/fidl/cpp/wire/service_handler_interface.h>

#include <map>

namespace driver {

// Callback invoked when a request is made to a FIDL protocol server end.
using AnyHandler = fit::function<void(fidl::internal::AnyTransport)>;

// Callback invoked when a request is made to a protocol server end.
template <typename Protocol>
using TypedHandler = fit::function<void(fidl::internal::ServerEndType<Protocol> request)>;

// A handler for an instance of a FIDL Service.
class ServiceInstanceHandler final : public fidl::ServiceHandlerInterface {
 public:
  ServiceInstanceHandler() = default;

  // Disable copying.
  ServiceInstanceHandler(const ServiceInstanceHandler&) = delete;
  ServiceInstanceHandler& operator=(const ServiceInstanceHandler&) = delete;

  // Enable moving.
  ServiceInstanceHandler(ServiceInstanceHandler&&) = default;
  ServiceInstanceHandler& operator=(ServiceInstanceHandler&&) = default;

 private:
  friend class OutgoingDirectory;

  // Return all registered member handlers. Key contains member name. Value
  // contains |Connector| func.
  //
  // Once taken, the `ServiceInstanceHandler` is no longer safe to use.
  std::map<std::string, AnyHandler> TakeMemberHandlers() { return std::move(handlers_); }

  // Add a |member| to the instance, whose connection will be handled by |handler|.
  //
  // # Errors
  //
  // ZX_ERR_ALREADY_EXISTS: The member already exists.
  zx::result<> AddAnyMember(std::string_view member, AnyMemberHandler handler) override {
    const std::string owned_member = std::string(member);
    if (handlers_.count(owned_member) != 0) {
      return zx::make_result(ZX_ERR_ALREADY_EXISTS);
    }

    // Since AnyMemberHandler is a protected type of this class' parent class,
    // |fidl::ServiceHandlerInterface|, wrap the type into a public one.
    AnyHandler bridge_func =
        [handler = std::move(handler)](fidl::internal::AnyTransport request_channel) {
          handler(std::move(request_channel));
        };
    handlers_[owned_member] = std::move(bridge_func);
    return zx::ok();
  }

  std::map<std::string, AnyHandler> handlers_ = {};
};

}  // namespace driver

#endif  // LIB_DRIVER2_HANDLERS_H_
