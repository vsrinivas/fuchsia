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
  //
  // TODO(fxbug.dev/75485): |Unbound| is deprecated as it tends to introduce
  // use-after-free. Switch to overriding |on_fidl_error| instead.
  virtual void Unbound(::fidl::UnbindInfo info) {}

  // |on_fidl_error| is invoked when the client encounters a terminal error:
  //
  // - The server-end of the channel was closed.
  // - An epitaph was received.
  // - Decoding or encoding failed.
  // - An invalid or unknown message was encountered.
  // - Error waiting on, reading from, or writing to the channel.
  //
  // It uses snake-case to differentiate from virtual methods corresponding to
  // FIDL events.
  //
  // |info| contains the detailed reason for stopping message dispatch.
  //
  // |on_fidl_error| will be invoked on a dispatcher thread, unless the user
  // shuts down the async dispatcher while there are active client bindings
  // associated with it. In that case, |on_fidl_error| will be synchronously
  // invoked on the thread calling dispatcher shutdown.
  //
  // TODO(fxbug.dev/75485): Remove the `final` qualification such that users
  // may listen for errors.
  virtual void on_fidl_error(::fidl::UnbindInfo error) final {}
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_INTERNAL_CLIENT_DETAILS_H_
