// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_SERVICE_HANDLER_BASE_H_
#define LIB_FIDL_CPP_SERVICE_HANDLER_BASE_H_

#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/interface_request.h>

namespace fidl {

// A base class for service handlers.
//
// This base class is used to reduce the number of libraries that generated code
// needs to depend upon.
class ServiceHandlerBase {
 public:
  virtual ~ServiceHandlerBase() = default;

  // A callback to be invoked when binding a channel to a protocol.
  using MemberHandler = fit::function<void(zx::channel channel, async_dispatcher_t* dispatcher)>;

  // Add a |member| to the instance, which will be handled by |handler|.
  virtual zx_status_t AddMember(std::string member, MemberHandler handler) const = 0;

  // Add a |member| to the instance, which will be handled by |handler|.
  template <typename Protocol>
  zx_status_t AddMember(std::string member, fidl::InterfaceRequestHandler<Protocol> handler) {
    return AddMember(std::move(member), [handler = std::move(handler)](
                                            zx::channel channel, async_dispatcher_t* dispatcher) {
      handler(fidl::InterfaceRequest<Protocol>(std::move(channel)));
    });
  }
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_SERVICE_HANDLER_BASE_H_
