// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_WIRE_MESSAGING_H_
#define LIB_FIDL_LLCPP_WIRE_MESSAGING_H_

#include <lib/fidl/llcpp/client_end.h>

namespace fidl {

// SyncClient owns a client endpoint and exposes synchronous FIDL calls.
template <typename FidlProtocol>
class WireSyncClient;

// This is the wire async client for the given protocol.
template <typename FidlProtocol>
class WireClient;

// This is the wire sync event handler for the given protocol.
template <typename FidlProtocol>
class WireSyncEventHandler;

// AsyncEventHandler is used by asynchronous clients and adds a callback
// for unbind completion on top of EventHandlerInterface.
template <typename FidlProtocol>
class WireAsyncEventHandler;

// Pure-virtual interface to be implemented by a server.
// This interface uses typed channels (i.e. |fidl::ClientEnd<SomeProtocol>|
// and |fidl::ServerEnd<SomeProtocol>|).
template <typename FidlProtocol>
class WireInterface;

// Deprecated transitional un-typed interface.
template <typename FidlProtocol>
class WireRawChannelInterface;

// EventSender owns a server endpoint and exposes methods for sending events.
template <typename FidlProtocol>
class WireEventSender;

template <typename FidlMethod>
struct WireRequest;

template <typename FidlMethod>
struct WireResponse;

template <typename FidlMethod>
class WireResult;

template <typename FidlMethod>
class WireUnownedResult;

namespace internal {

// WeakEventSender borrows the server endpoint from a binding object and
// exposes methods for sending events.
template <typename FidlProtocol>
class WireWeakEventSender;

// ClientImpl implements both synchronous and asynchronous FIDL calls,
// working together with the |::fidl::internal::ClientBase| class to safely
// borrow channel ownership from the binding object.
template <typename FidlProtocol>
class WireClientImpl;

template <typename FidlProtocol>
class WireEventHandlerInterface;

template <typename FidlProtocol>
class WireCaller;

}  // namespace internal

// |WireCall| is used to make method calls directly on a |fidl::ClientEnd|
// without having to set up a client. Call it like:
//   WireCall(client_end).Method(args...);
template <typename FidlProtocol>
fidl::internal::WireCaller<FidlProtocol> WireCall(const fidl::ClientEnd<FidlProtocol>& client_end) {
  return fidl::internal::WireCaller<FidlProtocol>(client_end.borrow());
}

// |WireCall| is used to make method calls directly on a |fidl::ClientEnd|
// without having to set up a client. Call it like:
//   WireCall(client_end).Method(args...);
template <typename FidlProtocol>
fidl::internal::WireCaller<FidlProtocol> WireCall(
    const fidl::UnownedClientEnd<FidlProtocol>& client_end) {
  return fidl::internal::WireCaller<FidlProtocol>(client_end);
}

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_WIRE_MESSAGING_H_
