// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_CLIENT_H_
#define LIB_FIDL_LLCPP_CLIENT_H_

#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl/llcpp/client_end.h>
#include <lib/fidl/llcpp/wire_messaging.h>

namespace fidl {

// A client for sending and receiving wire messages. It provides methods for
// binding a channel's client end to a dispatcher, unbinding the channel, and
// recovering the channel. Generated FIDL APIs are accessed by 'dereferencing'
// the |Client|:
//
//     // Creates a client that speaks over |client_end|, on the |my_dispatcher| dispatcher.
//     fidl::Client client(std::move(client_end), my_dispatcher);
//
//     // Call the |Foo| method asynchronously, passing in a callback that will be
//     // invoked on a dispatcher thread when the server response arrives.
//     auto status = client->Foo(args, [] (Result result) {});
//
// ## Lifecycle
//
// A client must be **bound** to an endpoint before it could be used. Binding a
// client to an endpoint starts the monitoring of incoming messages on that
// endpoint. Those messages are appropriately dispatched: to response callbacks,
// to event handlers, etc. FIDL methods (asyncrhonous or synchronous) may only
// be invoked on a bound client.
//
// A client may be default-constructed first then bound to an endpoint later via
// |Bind|, or constructed with a |ClientEnd|. When a client is constructed with
// a |ClientEnd|, it is as if that client had been default-constructed then
// later bound to that endpoint via |Bind|.
//
// To stop the monitoring of incoming messages, one may **teardown** the client.
// When teardown is initiated, the client will not monitor new messages on the
// endpoint. Ongoing callbacks will be allowed to run to completion. When
// teardown is complete, further calls on the same client will fail.
// Un-fulfilled response callbacks will be dropped.
//
// Destruction of a client object will initiate teardown.
//
// A |Client| may be |Clone|d, with the clone referencing the same endpoint.
// Automatic teardown occurs when the last |Client| bound to the endpoint is
// destructed.
//
// |Unbind| may be called on a |Client| to explicitly initiate teardown.
//
// |WaitForChannel| unbinds the endpoint from the client, allowing the endpoint
// to be recovered as the return value. Care must be taken when using this
// function, as it will be waiting for any synchronous calls to finish, and will
// forget about any in-progress asynchronous calls.
//
// TODO(fxbug.dev/68742): We may want to also wait for asynchronous calls, or
// panic when there are in-flight asynchronous calls.
//
// ## Thread safety
//
// FIDL method calls on this class are thread-safe. |Unbind|, |Clone|, and
// |WaitForChannel| are also thread-safe, and may be invoked in parallel with
// FIDL method calls. However, those operations must be synchronized with
// operations that consume or mutate the |Client| itself:
//
// - Binding the client to a new endpoint.
// - Assigning a new value to the |Client| variable.
// - Moving the |Client| to a different location.
// - Destroying the |Client| variable.
//
template <typename Protocol>
class Client final {
  using ClientImpl = fidl::internal::WireClientImpl<Protocol>;

 public:
  // Create an initialized Client which manages the binding of the client end of
  // a channel to a dispatcher.
  //
  // It is a logic error to use a dispatcher that is shutting down or already
  // shut down. Doing so will result in a panic.
  //
  // If any other error occurs during initialization, the |event_handler->Unbound|
  // handler will be invoked asynchronously with the reason, if specified.
  template <typename AsyncEventHandler = fidl::WireAsyncEventHandler<Protocol>>
  Client(fidl::ClientEnd<Protocol> client_end, async_dispatcher_t* dispatcher,
         std::shared_ptr<AsyncEventHandler> event_handler = nullptr) {
    Bind(std::move(client_end), dispatcher, std::move(event_handler));
  }

  // Create an uninitialized Client.
  //
  // Prefer using the constructor overload that binds the client to a channel
  // atomically during construction. Use this default constructor only when the
  // client must be constructed first before a channel could be obtained (for
  // example, if the client is an instance variable).
  Client() = default;

  // Returns if the |Client| is initialized.
  bool is_valid() const { return controller_.is_valid(); }
  explicit operator bool() const { return is_valid(); }

  // If the current |Client| is the last instance controlling the current
  // connection, the destructor of this |Client| will trigger unbinding, which
  // will cause any strong references to the |ClientBase| to be released.
  //
  // When the last |Client| destructs:
  // - The channel will be closed.
  // - Pointers obtained via |get| will be invalidated.
  // - Unbinding will happen, implying:
  //   * In-progress calls will be forgotten. Async callbacks will be dropped.
  //   * The |Unbound| callback in the |event_handler| will be invoked, if one
  //     was specified when creating or binding the client, on a dispatcher
  //     thread.
  //
  // See also: |Unbind|.
  ~Client() = default;

  // |fidl::Client|s can be safely moved without affecting any in-flight FIDL
  // method calls. Note that calling methods on a client should be serialized
  // with respect to operations that consume the client, such as moving it or
  // destroying it.
  Client(Client&& other) noexcept = default;
  Client& operator=(Client&& other) noexcept = default;

  // Bind the |client_end| endpoint to the dispatcher. If Client is already
  // initialized, destroys the previous binding, releasing its channel.
  //
  // It is a logic error to invoke |Bind| on a dispatcher that is shutting down
  // or already shut down. Doing so will result in a panic.
  //
  // When other errors occur during binding, the |event_handler->Unbound|
  // handler will be asynchronously invoked with the reason, if specified.
  //
  // Re-binding a |Client| to a different channel is equivalent to replacing the
  // |Client| with a new instance. TODO(fxbug.dev/78361): Disallow this
  // re-binding behavior.
  void Bind(fidl::ClientEnd<Protocol> client_end, async_dispatcher_t* dispatcher,
            std::shared_ptr<fidl::WireAsyncEventHandler<Protocol>> event_handler = nullptr) {
    controller_.Bind(std::make_shared<ClientImpl>(), client_end.TakeChannel(), dispatcher,
                     std::move(event_handler));
  }

  // Begins to unbind the channel from the dispatcher. May be called from any
  // thread. If provided, the |fidl::WireAsyncEventHandler<Protocol>::Unbound|
  // is invoked asynchronously on a dispatcher thread.
  //
  // NOTE: |Bind| must have been called before this.
  //
  // WARNING: While it is safe to invoke |Unbind| from any thread, it is unsafe
  // to wait on the |fidl::WireAsyncEventHandler<Protocol>::Unbound| from a
  // dispatcher thread, as that will likely deadlock.
  //
  // Unbinding can happen automatically via RAII. |Client|s will release
  // resources automatically when they are destructed. See also: |~Client|.
  void Unbind() { controller_.Unbind(); }

  // Returns another |Client| instance sharing the same channel.
  //
  // Prefer to |Clone| only when necessary e.g. extending the lifetime of a
  // |Client| to a different scope. Any living clone will prevent the cleanup of
  // the channel, unless one explicitly call |WaitForChannel|.
  Client Clone() { return Client(*this); }

  // Returns the underlying channel. Unbinds from the dispatcher if required.
  //
  // NOTE: |Bind| must have been called before this.
  //
  // WARNING: This is a blocking call. It waits for completion of dispatcher
  // unbind and of any channel operations, including synchronous calls which may
  // block indefinitely. It should not be invoked on the dispatcher thread if
  // the dispatcher is single threaded.
  fidl::ClientEnd<Protocol> WaitForChannel() {
    return fidl::ClientEnd<Protocol>(controller_.WaitForChannel());
  }

  // Returns the interface for making outgoing FIDL calls. If the client has
  // been unbound, calls on the interface return error with status
  // |ZX_ERR_CANCELED| and reason |fidl::Reason::kUnbind|.
  //
  // Persisting this pointer to a local variable is discouraged, since that
  // results in unsafe borrows. Always prefer making calls directly via the
  // |fidl::Client| reference-counting type. A client may be cloned and handed
  // off through the |Clone| method.
  ClientImpl* operator->() const { return get(); }
  ClientImpl& operator*() const { return *get(); }

 private:
  ClientImpl* get() const { return static_cast<ClientImpl*>(controller_.get()); }

  Client(const Client& other) noexcept = default;
  Client& operator=(const Client& other) noexcept = default;

  internal::ClientController controller_;
};

template <typename Protocol, typename AsyncEventHandlerReference>
Client(fidl::ClientEnd<Protocol>, async_dispatcher_t*, AsyncEventHandlerReference&&)
    -> Client<Protocol>;

template <typename Protocol>
Client(fidl::ClientEnd<Protocol>, async_dispatcher_t*) -> Client<Protocol>;

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_CLIENT_H_
