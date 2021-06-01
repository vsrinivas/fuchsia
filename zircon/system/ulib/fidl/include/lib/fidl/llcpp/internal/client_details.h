// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_INTERNAL_CLIENT_DETAILS_H_
#define LIB_FIDL_LLCPP_INTERNAL_CLIENT_DETAILS_H_

#include <lib/fidl/llcpp/result.h>

namespace fidl {
namespace internal {

// The base class for all asynchronous event handlers, regardless of domain
// object flavor or protocol type.
class AsyncEventHandler {
 public:
  virtual ~AsyncEventHandler() = default;

  // |Unbound| is invoked when the client endpoint has been disassociated from
  // the message dispatcher. See documentation on |fidl::Client| for the
  // lifecycle of a client object.
  //
  // |info| contains the detailed reason for stopping message dispatch.
  //
  // |Unbound| will be invoked on a dispatcher thread, unless the user shuts
  // down the async dispatcher while there are active client bindings associated
  // with it. In that case, |Unbound| will be synchronously invoked on the
  // thread calling dispatcher shutdown.
  virtual void Unbound(::fidl::UnbindInfo info) {}
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_INTERNAL_CLIENT_DETAILS_H_
