// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_WIRE_MESSAGING_H_
#define LIB_FIDL_LLCPP_WIRE_MESSAGING_H_

#include <lib/fit/function.h>
#ifdef __Fuchsia__
#include <lib/fidl/llcpp/client_end.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fidl/llcpp/transaction.h>
#include <lib/fidl/llcpp/transport.h>
#include <zircon/fidl.h>
#endif  // __Fuchsia__

namespace fidl {

template <typename FidlProtocol>
struct Transport;

template <typename FidlMethod>
struct WireRequest;

template <typename FidlMethod>
struct WireResponse;

#ifdef __Fuchsia__
// WireSyncClient owns a client endpoint and exposes synchronous FIDL calls.
template <typename FidlProtocol>
class WireSyncClient;

// WireClient implements a client and exposes both synchronous and asynchronous
// calls.
template <typename FidlProtocol>
class WireClient;

// WireSyncEventHandler is used by synchronous clients to handle events for the
// given protocol.
template <typename FidlProtocol>
class WireSyncEventHandler;

// WireAsyncEventHandler is used by asynchronous clients and adds a callback
// for unbind completion on top of EventHandlerInterface.
template <typename FidlProtocol>
class WireAsyncEventHandler;

// WireServer is a pure-virtual interface to be implemented by a server.
// This interface uses typed channels (i.e. |fidl::ClientEnd<SomeProtocol>|
// and |fidl::ServerEnd<SomeProtocol>|).
template <typename FidlProtocol>
class WireServer;

// WireEventSender owns a server endpoint and exposes methods for sending
// events.
template <typename FidlProtocol>
class WireEventSender;

template <typename FidlMethod>
class WireResponseContext;

template <typename FidlMethod>
class WireResult;

template <typename FidlMethod>
class WireUnownedResult;

template <typename FidlMethod>
using WireClientCallback = ::fit::callback<void(::fidl::WireUnownedResult<FidlMethod>&)>;

namespace internal {

// WireWeakEventSender borrows the server endpoint from a binding object and
// exposes methods for sending events.
template <typename FidlProtocol>
class WireWeakEventSender;

// WireClientImpl implements both synchronous and asynchronous FIDL calls,
// working together with the |::fidl::internal::ClientBase| class to safely
// borrow channel ownership from the binding object.
template <typename FidlProtocol>
class WireClientImpl;

template <typename FidlProtocol>
class WireEventHandlerInterface;

template <typename FidlProtocol>
class WireCaller;

template <typename FidlProtocol>
struct WireServerDispatcher;

template <typename FidlMethod>
class WireRequestView {
 public:
  WireRequestView(fidl::WireRequest<FidlMethod>* request) : request_(request) {}
  fidl::WireRequest<FidlMethod>* operator->() const { return request_; }

 private:
  fidl::WireRequest<FidlMethod>* request_;
};

template <typename FidlMethod>
class WireCompleterBase;

template <typename FidlMethod>
struct WireMethodTypes {
  using Completer = fidl::Completer<>;
};

template <typename FidlMethod>
using WireCompleter = typename fidl::internal::WireMethodTypes<FidlMethod>::Completer;

}  // namespace internal

// |WireCall| is used to make method calls directly on a |fidl::ClientEnd|
// without having to set up a client. Call it like:
//
//     fidl::WireCall(client_end).Method(args...);
template <typename FidlProtocol>
fidl::internal::WireCaller<FidlProtocol> WireCall(const fidl::ClientEnd<FidlProtocol>& client_end) {
  return fidl::internal::WireCaller<FidlProtocol>(client_end.borrow());
}

// |WireCall| is used to make method calls directly on a |fidl::ClientEnd|
// without having to set up a client. Call it like:
//
//     fidl::WireCall(client_end).Method(args...);
template <typename FidlProtocol>
fidl::internal::WireCaller<FidlProtocol> WireCall(
    const fidl::UnownedClientEnd<FidlProtocol>& client_end) {
  return fidl::internal::WireCaller<FidlProtocol>(client_end);
}

enum class DispatchResult;

// Dispatches the incoming message to one of the handlers functions in the protocol.
//
// This function should only be used in very low-level code, such as when manually
// dispatching a message to a server implementation.
//
// If there is no matching handler, it closes all the handles in |msg| and notifies
// |txn| of the error.
//
// Ownership of handles in |msg| are always transferred to the callee.
//
// The caller does not have to ensure |msg| has a |ZX_OK| status. It is idiomatic to pass a |msg|
// with potential errors; any error would be funneled through |InternalError| on the |txn|.
template <typename FidlProtocol>
void WireDispatch(fidl::WireServer<FidlProtocol>* impl, fidl::IncomingMessage&& msg,
                  fidl::Transaction* txn) {
  fidl::internal::WireServerDispatcher<FidlProtocol>::Dispatch(impl, std::move(msg), txn);
}

// Attempts to dispatch the incoming message to a handler function in the server implementation.
//
// This function should only be used in very low-level code, such as when manually
// dispatching a message to a server implementation.
//
// If there is no matching handler, it returns |fidl::DispatchResult::kNotFound|, leaving the
// message and transaction intact. In all other cases, it consumes the message and returns
// |fidl::DispatchResult::kFound|. It is possible to chain multiple TryDispatch functions in this
// manner.
//
// The caller does not have to ensure |msg| has a |ZX_OK| status. It is idiomatic to pass a |msg|
// with potential errors; any error would be funneled through |InternalError| on the |txn|.
template <typename FidlProtocol>
fidl::DispatchResult WireTryDispatch(fidl::WireServer<FidlProtocol>* impl,
                                     fidl::IncomingMessage& msg, fidl::Transaction* txn) {
  return fidl::internal::WireServerDispatcher<FidlProtocol>::TryDispatch(impl, msg, txn);
}
#endif  // __Fuchsia__

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_WIRE_MESSAGING_H_
