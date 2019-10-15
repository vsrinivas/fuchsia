// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_SERVICE_HANDLER_INTERFACE_H_
#define LIB_FIDL_LLCPP_SERVICE_HANDLER_INTERFACE_H_

#include <lib/fidl/llcpp/string_view.h>
#include <lib/fit/function.h>
#include <lib/fit/result.h>
#include <lib/fit/string_view.h>
#include <zircon/fidl.h>

#ifdef __Fuchsia__
#include <lib/zx/channel.h>
#endif  // __Fuchsia__

namespace llcpp::fidl {

#ifdef __Fuchsia__

// Interface used by generated FIDL code for adding protocol members to a Service instance.
// NOTE: This class is copied from the high-level C++ FIDL library in //sdk/lib/fidl/cpp.
class ServiceHandlerInterface {
 public:
  virtual ~ServiceHandlerInterface() = default;

  // User-defined action for handling a connection attempt to a member protocol.
  using MemberHandler = fit::function<zx_status_t(zx::channel)>;

  // Add a |member| to the instance, whose connection will be handled by |handler|.
  //
  // # Errors
  //
  // ZX_ERR_ALREADY_EXISTS: The member already exists.
  virtual zx_status_t AddMember(fit::string_view member, MemberHandler handler) = 0;

  // NOTE: This class is missing an |AddMember| overload that uses types only present
  // in the high-level C++ library.
};

#endif  // __Fuchsia__

}  // namespace llcpp::fidl

#endif  // LIB_FIDL_LLCPP_SERVICE_HANDLER_INTERFACE_H_
