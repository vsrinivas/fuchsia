// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// channel.h is the "entrypoint header" that should be included when using the
// channel transport with LLCPP.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_CHANNEL_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_CHANNEL_H_

#include <lib/fidl/cpp/wire/client.h>
#include <lib/fidl/cpp/wire/internal/arrow.h>
#include <lib/fidl/cpp/wire/internal/transport_channel.h>
#include <lib/fidl/cpp/wire/server.h>
#include <lib/fidl/cpp/wire/sync_call.h>
#include <lib/fidl/cpp/wire/wire_messaging.h>
#include <lib/zx/result.h>

namespace fidl {

template <typename Protocol>
struct Endpoints {
  fidl::ClientEnd<Protocol> client;
  fidl::ServerEnd<Protocol> server;
};

// Creates a pair of Zircon channel endpoints speaking the |Protocol| protocol.
// Whenever interacting with LLCPP, using this method should be encouraged over
// |zx::channel::create|, because this method encodes the precise protocol type
// into its results at compile time.
//
// The return value is a result type wrapping the client and server endpoints.
// Given the following:
//
//     auto endpoints = fidl::CreateEndpoints<MyProtocol>();
//
// The caller should first ensure that |endpoints.is_ok() == true|, after which
// the channel endpoints may be accessed in one of two ways:
//
// - Direct:
//     endpoints->client;
//     endpoints->server;
//
// - Structured Binding:
//     auto [client_end, server_end] = std::move(endpoints.value());
template <typename Protocol>
zx::result<Endpoints<Protocol>> CreateEndpoints() {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return zx::error_result(status);
  }
  return zx::ok(Endpoints<Protocol>{
      fidl::ClientEnd<Protocol>(std::move(local)),
      fidl::ServerEnd<Protocol>(std::move(remote)),
  });
}

// Creates a pair of Zircon channel endpoints speaking the |Protocol| protocol.
// Whenever interacting with LLCPP, using this method should be encouraged over
// |zx::channel::create|, because this method encodes the precise protocol type
// into its results at compile time.
//
// This overload of |CreateEndpoints| may lead to more concise code when the
// caller already has the client endpoint defined as an instance variable.
// It will replace the destination of |out_client| with a newly created client
// endpoint, and return the corresponding server endpoint in a |zx::result|:
//
//     // |client_end_| is an instance variable.
//     auto server_end = fidl::CreateEndpoints(&client_end_);
//     if (server_end.is_ok()) { ... }
template <typename Protocol>
zx::result<fidl::ServerEnd<Protocol>> CreateEndpoints(fidl::ClientEnd<Protocol>* out_client) {
  auto endpoints = fidl::CreateEndpoints<Protocol>();
  if (!endpoints.is_ok()) {
    return endpoints.take_error();
  }
  *out_client = fidl::ClientEnd<Protocol>(std::move(endpoints->client));
  return zx::ok(fidl::ServerEnd<Protocol>(std::move(endpoints->server)));
}

// Creates a pair of Zircon channel endpoints speaking the |Protocol| protocol.
// Whenever interacting with LLCPP, using this method should be encouraged over
// |zx::channel::create|, because this method encodes the precise protocol type
// into its results at compile time.
//
// This overload of |CreateEndpoints| may lead to more concise code when the
// caller already has the server endpoint defined as an instance variable.
// It will replace the destination of |out_server| with a newly created server
// endpoint, and return the corresponding client endpoint in a |zx::result|:
//
//     // |server_end_| is an instance variable.
//     auto client_end = fidl::CreateEndpoints(&server_end_);
//     if (client_end.is_ok()) { ... }
template <typename Protocol>
zx::result<fidl::ClientEnd<Protocol>> CreateEndpoints(fidl::ServerEnd<Protocol>* out_server) {
  auto endpoints = fidl::CreateEndpoints<Protocol>();
  if (!endpoints.is_ok()) {
    return endpoints.take_error();
  }
  *out_server = fidl::ServerEnd<Protocol>(std::move(endpoints->server));
  return zx::ok(fidl::ClientEnd<Protocol>(std::move(endpoints->client)));
}

// This class manages a server connection over a Zircon channel and its binding
// to an |async_dispatcher_t*|, which may be multi-threaded. See the detailed
// documentation on the |BindServer| APIs.
template <typename Protocol>
class ServerBindingRef : public internal::ServerBindingRefBase {
 public:
  ~ServerBindingRef() = default;

  ServerBindingRef(ServerBindingRef&&) noexcept = default;
  ServerBindingRef& operator=(ServerBindingRef&&) noexcept = default;

  ServerBindingRef(const ServerBindingRef&) = default;
  ServerBindingRef& operator=(const ServerBindingRef&) = default;

  // Triggers an asynchronous unbind operation. If specified, |on_unbound| will
  // be asynchronously run on a dispatcher thread, passing in the endpoint and
  // the unbind reason.
  //
  // On return, the dispatcher will stop monitoring messages on the endpoint,
  // though handling of any already in-flight transactions will continue.
  // Pending completers may be discarded.
  //
  // This may be called from any thread.
  //
  // WARNING: While it is safe to invoke Unbind() from any thread, it is unsafe to wait on the
  // OnUnboundFn from a dispatcher thread, as that will likely deadlock.
  using ServerBindingRefBase::Unbind;

  // Triggers an asynchronous unbind operation. Eventually, the epitaph will be sent over the
  // channel which will be subsequently closed. If specified, |on_unbound| will be invoked giving
  // the unbind reason as an argument.
  //
  // This may be called from any thread.
  void Close(zx_status_t epitaph) {
    if (auto binding = ServerBindingRefBase::binding().lock())
      binding->Close(std::move(binding), epitaph);
  }

 private:
  // This is so that only |BindServerTypeErased| will be able to construct a
  // new instance of |ServerBindingRef|.
  friend internal::ServerBindingRefType<Protocol> internal::BindServerTypeErased<Protocol>(
      async_dispatcher_t* dispatcher, fidl::internal::ServerEndType<Protocol> server_end,
      internal::IncomingMessageDispatcher* interface, internal::AnyOnUnboundFn on_unbound);

  explicit ServerBindingRef(std::weak_ptr<internal::AsyncServerBinding> internal_binding)
      : ServerBindingRefBase(std::move(internal_binding)) {}
};

// |BindServer| starts handling message on |server_end| using implementation
// |impl|, on a potentially multi-threaded |dispatcher|. Multiple requests may
// be concurrently in-flight, and responded to synchronously or asynchronously.
//
// |ServerImpl| should implement the abstract base class
// |fidl::WireServer<library::MyProtocol>|, typically generated by the low-level
// C++ backend, corresponding to methods in the protocol |library.MyProtocol|.
//
// This function adds an asynchronous wait to the given |dispatcher| for new
// messages to arrive on |server_end|. When each message arrives, the
// corresponding method handler in |ServerImpl| is called on one of the
// threads of the |dispatcher|.
//
// ## Starting message dispatch
//
// On success, |BindServer| associates |impl| and |server_end| with the
// |dispatcher|, and begins handling messages that arrive on |server_end|. This
// association is called a "binding". The dispatcher owns the |server_end| while
// the binding is active.
//
// The returned |ServerBindingRef| is a reference to the binding; it does not
// own the binding. In particular, the binding is kept alive by the dispatcher
// even if the returned |ServerBindingRef| is dropped. If the binding reference
// is ignored, the server operates in a "self-managed" mode, where it will
// continue listening for messages until an error occurs or if the user tears
// down the connection using a |Completer|.
//
// It is a logic error to invoke |BindServer| on a dispatcher that is
// shutting down or already shut down. Doing so will result in a panic.
//
// If any other error occurs when creating the binding, the |on_unbound| handler
// will be invoked asynchronously with the reason. See the "Unbind" section
// for details on |on_unbound|.
//
// ## Stopping message dispatch
//
// ### Unbind
//
// |ServerBindingRef::Unbind| requests to explicitly disassociate the server
// |impl| and endpoint from the dispatcher, and to retrieve the |server_end|
// endpoint. Note that this is an asynchronous procedure.
//
// |Unbind| is guaranteed to return in a short and bounded amount of time. It
// does not depend on whether there are any in-flight requests. As such, the
// user may safely take locks around an |Unbind| call.
//
// After unbinding completes:
//
// - The |server_end| is detached from the dispatcher; no dispatcher threads
//   will interact with it.
// - Calls on |Completer| objects from in-flight requests will have no effect.
//   Failable operations will return |ZX_ERR_CANCELED|.
// - Subsequent calls made on the |ServerBindingRef| will be ignored. Failable
//   operations will return |ZX_ERR_CANCELED|.
// - If |on_unbound| is not specified, the |server_end| is closed.
// - If |on_unbound| is specified, it will be called to signal the completion.
//   Ownership of the |server_end| is transferred to this hook.
//
// |on_unbound| must be a callable of the following signature:
//
//     // |impl| is the pointer to the server implementation.
//     // |info| contains the reason for stopping the message dispatch.
//     // |server_end| is the server channel endpoint.
//     // |ProtocolType| is the type of the FIDL protocol.
//     void OnUnbound(ServerImpl* impl,
//                    fidl::UnbindInfo info,
//                    fidl::ServerEnd<ProtocolType> server_end);
//
// More precisely, if there is a dispatcher thread waiting for incoming messages
// on the channel, it will stop monitoring the channel and detach itself from
// the binding. If there is a thread executing the method handler, the channel
// would be pulled from underneath it, such that it may no longer make any
// replies. When no thread has any active reference to the channel, the
// |on_unbound| callback will be invoked.
//
// |on_unbound| will be executed on a |dispatcher| thread, unless the user shuts
// down the |dispatcher| while there are active bindings associated with it. In
// that case, those bindings will be synchronously unbound, and the |on_unbound|
// callback would be executed on the thread invoking shutdown. |on_unbound|
// hooks must not acquire any locks that could be held during |dispatcher|
// shutdown.
//
// ### Close
//
// |ServerBindingRef::Close| has the same effects as |Unbind| except that it
// sends an epitaph message on the |server_end|.
//
// If specified, the |on_unbound| hook will execute after the epitaph has been
// sent.
//
// ## Server implementation ownership
//
// The server instance |impl| must remain alive while it is bound to the message
// dispatcher. Take special note of |Unbind|, as it returns before the unbind
// operation has completed. It is only safe to destroy the server instance
// within or after |on_unbound|.
//
// This overload borrows the server implementation by raw pointer. There are
// additional overloads of |BindServer| that either takes ownership of an
// implementation via |std::unique_ptr|, or shares the ownership via
// |std::shared_ptr|. Using either of those smart pointer overloads would
// automatically ensure memory safety.
//
// ## Error conditions
//
// The server implementation can call |Close| on the completer to indicate an
// application error during message handling.
//
// The connection will also be automatically closed by the dispatching logic in
// certain conditions:
//
// - If the client-end of the channel is closed (ZX_ERR_PEER_CLOSED).
// - If an error occurs when waiting on, reading from, or writing to the
//   channel.
// - If decoding an incoming message fails or encoding an outgoing message
//   fails.
// - If the message was not defined in the FIDL protocol.
//
// These error conditions lead to the unbinding of the connection. If
// |on_unbound| was specified, it would be called on a |dispatcher| thread with
// a failure reason, allowing the user to process the error.
//
// ## Message ordering
//
// By default, the message dispatch function for a binding will only ever be
// invoked by a single |dispatcher| thread at a time, even if the dispatcher has
// multiple threads. Messages will be dispatched in the order that they are
// received on the channel.
//
// A method handler may call |EnableNextDispatch| on their completer to allow
// another thread to begin dispatching the next message before the current
// method handler returns. Of course, this is only meaningful if the
// |dispatcher| has multiple threads.
//
// If a particular user does not care about ordering, they may invoke
// |EnableNextDispatch| immediately in the message handler. If you have such a
// use case, please file a bug, where we may consider instead providing a mode
// to automatically parallelize.
//
// ## Template Ergonomics
//
// This function is able to infer the type of |ServerImpl| and |OnUnbound| in
// most cases. The following code would compile without explicitly specializing
// |BindServer|:
//
//     // Suppose |this| is a server implementation class |Foo|, that
//     // implements the |Bar| FIDL protocol.
//     fidl:ServerEnd<Bar> server_end = ...;
//     fidl::BindServer(dispatcher, std::move(server_end), this,
//                      [](Foo*, fidl::UnbindInfo, fidl::ServerEnd<Bar>) { ... });
//
// TODO(fxbug.dev/66343): Consider using a "DidUnbind" virtual function
// in the server interface to replace the |on_unbound| handler lambda.
template <typename ServerImpl, typename OnUnbound = std::nullptr_t>
ServerBindingRef<typename ServerImpl::_EnclosingProtocol> BindServer(
    async_dispatcher_t* dispatcher,
    fidl::internal::ServerEndType<typename ServerImpl::_EnclosingProtocol> server_end,
    ServerImpl* impl, OnUnbound&& on_unbound = nullptr) {
  static_assert(std::is_same_v<typename ServerImpl::_EnclosingProtocol::Transport,
                               fidl::internal::ChannelTransport>);
  return internal::BindServerImpl<ServerImpl>(
      dispatcher, std::move(server_end), impl,
      internal::UnboundThunk(std::move(impl), std::forward<OnUnbound>(on_unbound)));
}

// Overload of |BindServer| that takes ownership of the server as a |unique_ptr|.
// The pointer is destroyed on the same thread as the one calling |on_unbound|,
// and happens right after |on_unbound|. See |BindServer| for details.
template <typename ServerImpl, typename OnUnbound = std::nullptr_t>
ServerBindingRef<typename ServerImpl::_EnclosingProtocol> BindServer(
    async_dispatcher_t* dispatcher,
    fidl::internal::ServerEndType<typename ServerImpl::_EnclosingProtocol> server_end,
    std::unique_ptr<ServerImpl>&& impl, OnUnbound&& on_unbound = nullptr) {
  static_assert(std::is_same_v<typename ServerImpl::_EnclosingProtocol::Transport,
                               fidl::internal::ChannelTransport>);
  ServerImpl* impl_raw = impl.get();
  return internal::BindServerImpl<ServerImpl>(
      dispatcher, std::move(server_end), impl_raw,
      internal::UnboundThunk(std::move(impl), std::forward<OnUnbound>(on_unbound)));
}

// Overload of |BindServer| that shares ownership of the server via a |shared_ptr|.
// The pointer is destroyed on the same thread as the one calling |on_unbound|,
// and happens right after |on_unbound|. See |BindServer| for details.
template <typename ServerImpl, typename OnUnbound = std::nullptr_t>
ServerBindingRef<typename ServerImpl::_EnclosingProtocol> BindServer(
    async_dispatcher_t* dispatcher,
    fidl::internal::ServerEndType<typename ServerImpl::_EnclosingProtocol> server_end,
    std::shared_ptr<ServerImpl> impl, OnUnbound&& on_unbound = nullptr) {
  static_assert(std::is_same_v<typename ServerImpl::_EnclosingProtocol::Transport,
                               fidl::internal::ChannelTransport>);
  ServerImpl* impl_raw = impl.get();
  return internal::BindServerImpl<ServerImpl>(
      dispatcher, std::move(server_end), impl_raw,
      internal::UnboundThunk(std::move(impl), std::forward<OnUnbound>(on_unbound)));
}

// |fidl::WireSyncClient| owns a client endpoint and exposes synchronous FIDL
// calls. Prefer using this owning class over |fidl::WireCall| unless one has to
// interface with very low-level functionality (such as making a call over a raw
// zx_handle_t).
//
// Generated FIDL APIs are accessed by 'dereferencing' the client value:
//
//     // Creates a sync client that speaks over |client_end|.
//     fidl::WireSyncClient client(std::move(client_end));
//
//     // Call the |Foo| method synchronously, obtaining the results from the
//     // return value.
//     fidl::WireResult result = client->Foo(args);
//
// |fidl::WireSyncClient| is suitable for code without access to an async
// dispatcher.
//
// ## Thread safety
//
// |WireSyncClient| is generally thread-safe with a few caveats:
//
// - Client objects can be safely sent between threads.
// - One may invoke many FIDL methods in parallel on the same client. However,
//   FIDL method calls must be synchronized with operations that consume or
//   mutate the client object itself:
//
//     - Calling `Bind` or `TakeClientEnd`.
//     - Assigning a new value to the |WireSyncClient| variable.
//     - Moving the |WireSyncClient| to a different location.
//     - Destroying the |WireSyncClient|.
//
// - There can be at most one `HandleOneEvent` call going on at the same time.
template <typename FidlProtocol>
class WireSyncClient {
 public:
  // Creates an uninitialized client that is not bound to a client endpoint.
  //
  // Prefer using the constructor overload that initializes the client
  // atomically during construction. Use this default constructor only when the
  // client must be constructed first before an endpoint could be obtained (for
  // example, if the client is an instance variable).
  //
  // The client may be initialized later via |Bind|.
  WireSyncClient() = default;

  // Creates an initialized client. FIDL calls will be made on |client_end|.
  //
  // Similar to |fidl::WireClient|, the client endpoint must be valid.
  //
  // To just make a FIDL call uniformly on a client endpoint that may or may not
  // be valid, use the |fidl::WireCall(client_end)| helper. We may extend
  // |fidl::WireSyncClient<P>| with richer features hinging on having a valid
  // endpoint in the future.
  explicit WireSyncClient(::fidl::ClientEnd<FidlProtocol> client_end)
      : client_end_(std::move(client_end)) {
    ZX_ASSERT(is_valid());
  }

  ~WireSyncClient() = default;
  WireSyncClient(WireSyncClient&&) noexcept = default;
  WireSyncClient& operator=(WireSyncClient&&) noexcept = default;

  // Whether the client is initialized.
  bool is_valid() const { return client_end_.is_valid(); }
  explicit operator bool() const { return is_valid(); }

  // Borrows the underlying client endpoint. The client must have been
  // initialized.
  const ::fidl::ClientEnd<FidlProtocol>& client_end() const {
    ZX_ASSERT(is_valid());
    return client_end_;
  }

  // Initializes the client with a |client_end|. FIDL calls will be made on this
  // endpoint.
  //
  // It is not allowed to call |Bind| on an initialized client. To rebind a
  // |WireSyncClient| to a different endpoint, simply replace the
  // |WireSyncClient| variable with a new instance.
  void Bind(::fidl::ClientEnd<FidlProtocol> client_end) {
    ZX_ASSERT(!is_valid());
    client_end_ = std::move(client_end);
    ZX_ASSERT(is_valid());
  }

  // Extracts the underlying endpoint from the client. After this operation, the
  // client goes back to an uninitialized state.
  //
  // It is not safe to invoke this method while there are ongoing FIDL calls.
  ::fidl::ClientEnd<FidlProtocol> TakeClientEnd() {
    ZX_ASSERT(is_valid());
    return std::move(client_end_);
  }

  // Returns an interface for making FIDL calls with managed memory.
  internal::SyncEndpointManagedVeneer<internal::WireSyncClientImpl<FidlProtocol>> operator->()
      const {
    ZX_ASSERT(is_valid());
    return internal::SyncEndpointManagedVeneer<internal::WireSyncClientImpl<FidlProtocol>>(
        fidl::internal::MakeAnyUnownedTransport(client_end_.handle()));
  }

  // Returns an interface which exposes the caller-allocating API, using
  // the provided |resource| to allocate buffers necessary for each call.
  // See documentation on |SyncEndpointVeneer::buffer| for detailed behavior.
  template <typename MemoryResource>
  auto buffer(MemoryResource&& resource) const {
    ZX_ASSERT(is_valid());
    return internal::SyncEndpointBufferVeneer<internal::WireSyncBufferClientImpl<FidlProtocol>>{
        internal::MakeAnyUnownedTransport(client_end_.handle()),
        internal::MakeAnyBufferAllocator(std::forward<MemoryResource>(resource))};
  }

  // Handle all possible events defined in this protocol.
  //
  // Blocks to consume exactly one message from the channel, then call the corresponding virtual
  // method defined in |event_handler|. If the message was unknown or malformed, returns an
  // error without calling any virtual method.
  ::fidl::Status HandleOneEvent(fidl::WireSyncEventHandler<FidlProtocol>& event_handler) const {
    return event_handler.HandleOneEvent(client_end());
  }

 private:
  ::fidl::ClientEnd<FidlProtocol> client_end_;
};

template <typename FidlProtocol>
WireSyncClient(fidl::ClientEnd<FidlProtocol>) -> WireSyncClient<FidlProtocol>;

// |WireCall| is used to make method calls directly on a |fidl::ClientEnd|
// without having to set up a client. Call it like:
//
//     fidl::WireCall(client_end)->Method(args...);
//
template <typename FidlProtocol>
internal::SyncEndpointVeneer<internal::WireSyncClientImpl, FidlProtocol> WireCall(
    const fidl::ClientEnd<FidlProtocol>& client_end) {
  return internal::SyncEndpointVeneer<internal::WireSyncClientImpl, FidlProtocol>(
      fidl::internal::MakeAnyUnownedTransport(client_end.borrow().handle()));
}

// |WireCall| is used to make method calls directly on a |fidl::ClientEnd|
// without having to set up a client. Call it like:
//
//     fidl::WireCall(client_end)->Method(args...);
//
template <typename FidlProtocol>
internal::SyncEndpointVeneer<internal::WireSyncClientImpl, FidlProtocol> WireCall(
    const fidl::UnownedClientEnd<FidlProtocol>& client_end) {
  return internal::SyncEndpointVeneer<internal::WireSyncClientImpl, FidlProtocol>(
      fidl::internal::MakeAnyUnownedTransport(client_end.handle()));
}

// Return an interface for sending FIDL events containing wire domain objects
// over the endpoint managed by |binding_ref|. Call it like:
//
//     fidl::WireSendEvent(server_binding_ref)->FooEvent(args...);
//
template <typename FidlProtocol>
internal::WeakEventSenderVeneer<internal::WireWeakEventSender, FidlProtocol> WireSendEvent(
    const ServerBindingRef<FidlProtocol>& binding_ref) {
  return internal::WeakEventSenderVeneer<internal::WireWeakEventSender, FidlProtocol>(
      internal::BorrowBinding(
          static_cast<const fidl::internal::ServerBindingRefBase&>(binding_ref)));
}

// Return an interface for sending FIDL events containing wire domain objects
// over |server_end|. Call it like:
//
//     fidl::WireSendEvent(server_end)->FooEvent(args...);
//
template <typename FidlProtocol>
internal::SyncEndpointVeneer<internal::WireEventSender, FidlProtocol> WireSendEvent(
    const ServerEnd<FidlProtocol>& server_end) {
  return internal::SyncEndpointVeneer<internal::WireEventSender, FidlProtocol>(
      fidl::internal::MakeAnyUnownedTransport(server_end.channel()));
}

// Return an interface for sending FIDL events containing wire domain objects
// over |server_end|. Call it like:
//
//     fidl::WireSendEvent(server_end)->FooEvent(args...);
//
template <typename FidlProtocol>
internal::SyncEndpointVeneer<internal::WireEventSender, FidlProtocol> WireSendEvent(
    UnownedServerEnd<FidlProtocol> server_end) {
  return internal::SyncEndpointVeneer<internal::WireEventSender, FidlProtocol>(
      fidl::internal::MakeAnyUnownedTransport(server_end.handle()));
}

}  // namespace fidl

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_CHANNEL_H_
