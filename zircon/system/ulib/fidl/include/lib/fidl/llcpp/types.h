// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_TYPES_H_
#define LIB_FIDL_LLCPP_TYPES_H_

#include <lib/fit/function.h>
#include <zircon/types.h>

namespace fidl {

// UnbindInfo is passed to the OnUnboundFn if provided by the user.
struct UnbindInfo {
  // Reason for unbinding the channel.
  enum Reason {
    // The user invoked Unbind(). status is ZX_OK.
    kUnbind,

    // Server only. The user invoked Close(epitaph) on a ServerBindingRef or Completer and the
    // epitaph was sent. status is the result of sending the epitaph.
    kClose,

    // The channel peer was closed. For a server, status is ZX_ERR_PEER_CLOSED. For a client, it is
    // the epitaph. If no epitaph was sent, the behavior is equivalent to having received a
    // ZX_ERR_PEER_CLOSED epitaph.
    kPeerClosed,

    // For the following reasons, status contains the associated error code.
    // NOTE: For a server, unlike kClose, the user is still responsible for sending an epitaph.

    // An error associated with the dispatcher.
    kDispatcherError,

    // An error associated with reading to/writing from the channel.
    kChannelError,

    // Failure to encode an outgoing message.
    kEncodeError,

    // Failure to decode an incoming message.
    kDecodeError,

    // A malformed message, message with unknown ordinal, unexpected reply, an unsupported event
    // was received.
    kUnexpectedMessage,
  };
  Reason reason;
  zx_status_t status;
};

// Invoked from a dispatcher thread after the server end of a channel is unbound.
template <typename Interface>
using OnUnboundFn = fit::callback<void(Interface*, UnbindInfo, zx::channel)>;

// Invoked from a dispatcher thread after the client end of a channel is unbound.
using OnClientUnboundFn = fit::callback<void(UnbindInfo, zx::channel)>;

}  // fidl

#endif  // LIB_FIDL_LLCPP_TYPES_H_
