// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_SERVICE_HANDLER_INTERFACE_H_
#define LIB_FIDL_LLCPP_SERVICE_HANDLER_INTERFACE_H_

#include <lib/fidl/llcpp/string_view.h>
#include <lib/fit/function.h>
#include <lib/fpromise/result.h>
#include <lib/stdcompat/string_view.h>
#include <zircon/fidl.h>

#ifdef __Fuchsia__
#include <lib/fidl/llcpp/internal/endpoints.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>
#endif  // __Fuchsia__

namespace fidl {

#ifdef __Fuchsia__

// Interface used by generated FIDL code for adding protocol members to a Service instance.
class ServiceHandlerInterface {
 public:
  virtual ~ServiceHandlerInterface() = default;

  // User-defined action for handling a connection attempt to a
  // member FIDL protocol defined by |Protocol|.
  template <typename Protocol>
  using MemberHandler = fit::function<zx::status<>(::fidl::ServerEnd<Protocol>)>;

  // Add a |member| to the instance, which will be handled by |handler|.
  //
  // This method specifies the exact protocol |Protocol|, hence should be
  // used by end-users adding service member handlers to a service directory.
  //
  // # Errors
  //
  // ZX_ERR_ALREADY_EXISTS: The member already exists.
  template <typename Protocol>
  zx::status<> AddMember(cpp17::string_view member, MemberHandler<Protocol> handler) {
    return AddAnyMember(member, [handler = std::move(handler)](zx::channel channel) {
      return handler(::fidl::ServerEnd<Protocol>(std::move(channel)));
    });
  }

 protected:
  // User-defined action for handling a connection attempt to any
  // member FIDL protocol.
  using AnyMemberHandler = fit::function<zx::status<>(zx::channel)>;

  // Add a |member| to the instance, whose connection will be handled by |handler|.
  //
  // This variant does not restrict on the protocol type, hence should be
  // implemented by service directories (typically filesystem servers)
  // which host arbitrary member protocols under |member| paths.
  //
  // # Errors
  //
  // ZX_ERR_ALREADY_EXISTS: The member already exists.
  virtual zx::status<> AddAnyMember(cpp17::string_view member, AnyMemberHandler handler) = 0;
};

#endif  // __Fuchsia__

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_SERVICE_HANDLER_INTERFACE_H_
