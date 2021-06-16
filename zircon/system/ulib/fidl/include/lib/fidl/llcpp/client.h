// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_CLIENT_H_
#define LIB_FIDL_LLCPP_CLIENT_H_

#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl/llcpp/client_end.h>
#include <lib/fidl/llcpp/wire_messaging.h>

namespace fidl {

// |fidl::ObserveTeardown| is used with |fidl::WireSharedClient| and allows
// custom logic to run on teardown completion, represented by a callable
// |callback| that takes no parameters and returns |void|. It should be supplied
// as the last argument when constructing or binding the client. See lifecycle
// notes on |fidl::WireSharedClient|.
template <typename Callable>
fidl::internal::AnyTeardownObserver ObserveTeardown(Callable&& callback) {
  static_assert(std::is_convertible<Callable, fit::closure>::value,
                "|callback| must have the signature `void fn()`.");
  return fidl::internal::AnyTeardownObserver::ByCallback(std::forward<Callable>(callback));
}

// |fidl::ShareUntilTeardown| configures a |fidl::WireSharedClient| to co-own
// the supplied |object| until teardown completion. It may be used to extend the
// lifetime of user objects responsible for handling messages. It should be
// supplied as the last argument when constructing or binding the client. See
// lifecycle notes on |fidl::WireSharedClient|.
template <typename T>
fidl::internal::AnyTeardownObserver ShareUntilTeardown(std::shared_ptr<T> object) {
  return fidl::internal::AnyTeardownObserver::ByOwning(object);
}

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
  //
  // TODO(fxbug.dev/75485): Take a raw pointer to the event handler.
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
  // Re-binding a |Client| to a different channel is equivalent to replacing
  // the |Client| with a new instance. TODO(fxbug.dev/78361): Disallow this
  // re-binding behavior.
  //
  // TODO(fxbug.dev/75485): Take a raw pointer to the event handler.
  void Bind(fidl::ClientEnd<Protocol> client_end, async_dispatcher_t* dispatcher,
            std::shared_ptr<fidl::WireAsyncEventHandler<Protocol>> event_handler = nullptr) {
    controller_.Bind(std::make_shared<ClientImpl>(), client_end.TakeChannel(), dispatcher,
                     event_handler.get(), fidl::ShareUntilTeardown(event_handler));
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

// TODO(fxbug.dev/75485): This class is not yet ready for general use.
template <typename Protocol>
class WireSharedClient final {
  using ClientImpl = fidl::internal::WireClientImpl<Protocol>;

 public:
  // Creates an initialized |WireSharedClient| which manages the binding of the
  // client end of a channel to a dispatcher.
  //
  // It is a logic error to use a dispatcher that is shutting down or already
  // shut down. Doing so will result in a panic.
  //
  // If any other error occurs during initialization, the
  // |event_handler->on_fidl_error| handler will be invoked asynchronously with
  // the reason, if specified.
  //
  // |event_handler| will be destroyed when teardown completes.
  template <typename AsyncEventHandler = fidl::WireAsyncEventHandler<Protocol>>
  WireSharedClient(fidl::ClientEnd<Protocol> client_end, async_dispatcher_t* dispatcher,
                   std::unique_ptr<AsyncEventHandler> event_handler) {
    Bind(std::move(client_end), dispatcher, std::move(event_handler));
  }

  // Creates a |WireSharedClient| that supports custom behavior on teardown
  // completion via |teardown_observer|. Through helpers that return an
  // |AnyTeardownObserver|, users may link the completion of teardown to the
  // invocation of a callback or the lifecycle of related business objects. See
  // for example |fidl::ObserveTeardown| and |fidl::ShareUntilTeardown|.
  //
  // This overload does not demand taking ownership of |event_handler| by
  // |std::unique_ptr|, hence is suitable when the |event_handler| needs to be
  // managed independently of the client lifetime.
  //
  // See |WireSharedClient| above for other behavior aspects of the constructor.
  template <typename AsyncEventHandler = fidl::WireAsyncEventHandler<Protocol>>
  WireSharedClient(fidl::ClientEnd<Protocol> client_end, async_dispatcher_t* dispatcher,
                   AsyncEventHandler* event_handler,
                   fidl::internal::AnyTeardownObserver teardown_observer =
                       fidl::internal::AnyTeardownObserver::Noop()) {
    Bind(std::move(client_end), dispatcher, event_handler, std::move(teardown_observer));
  }

  // Overload of |WireSharedClient| that omits the |event_handler|, to
  // workaround C++ limitations on default arguments.
  //
  // See |WireSharedClient| above for other behavior aspects of the constructor.
  template <typename AsyncEventHandler = fidl::WireAsyncEventHandler<Protocol>>
  WireSharedClient(fidl::ClientEnd<Protocol> client_end, async_dispatcher_t* dispatcher,
                   fidl::internal::AnyTeardownObserver teardown_observer =
                       fidl::internal::AnyTeardownObserver::Noop()) {
    Bind(std::move(client_end), dispatcher, nullptr, std::move(teardown_observer));
  }

  // Create an uninitialized |WireSharedClient|.
  //
  // Prefer using the constructor overload that binds the client to a channel
  // atomically during construction. Use this default constructor only when the
  // client must be constructed first before a channel could be obtained (for
  // example, if the client is an instance variable).
  WireSharedClient() = default;

  // Returns if the |WireSharedClient| is initialized.
  bool is_valid() const { return controller_.is_valid(); }
  explicit operator bool() const { return is_valid(); }

  // If the current |WireSharedClient| is the last instance controlling the current
  // connection, the destructor of this |WireSharedClient| will trigger unbinding, which
  // will cause any strong references to the |ClientBase| to be released.
  //
  // When the last |WireSharedClient| destructs:
  // - The channel will be closed.
  // - Pointers obtained via |get| will be invalidated.
  // - Unbinding will happen, implying:
  //   * In-progress calls will be forgotten. Async callbacks will be dropped.
  //   * The |Unbound| callback in the |event_handler| will be invoked, if one
  //     was specified when creating the client, on a dispatcher thread.
  //
  // See also: |Unbind|.
  ~WireSharedClient() = default;

  // |fidl::WireSharedClient|s can be safely moved without affecting any in-progress
  // operations. Note that calling methods on a client should be serialized with
  // respect to operations that consume the client, such as moving it or
  // destroying it.
  WireSharedClient(WireSharedClient&& other) noexcept = default;
  WireSharedClient& operator=(WireSharedClient&& other) noexcept = default;

  // Bind the |client_end| endpoint to the dispatcher. If |WireSharedClient| is already
  // initialized, destroys the previous binding, releasing its channel.
  //
  // It is a logic error to invoke |Bind| on a dispatcher that is shutting down
  // or already shut down. Doing so will result in a panic.
  //
  // When other error occurs during binding, the |event_handler->on_fidl_error|
  // handler will be asynchronously invoked with the reason, if specified.
  //
  // |event_handler| will be destroyed when teardown completes.
  //
  // Re-binding a |WireSharedClient| to a different channel is equivalent to replacing
  // the |WireSharedClient| with a new instance.
  void Bind(fidl::ClientEnd<Protocol> client_end, async_dispatcher_t* dispatcher,
            std::unique_ptr<fidl::WireAsyncEventHandler<Protocol>> event_handler) {
    auto event_handler_raw = event_handler.get();
    Bind(std::move(client_end), dispatcher, event_handler_raw,
         fidl::internal::AnyTeardownObserver::ByOwning(std::move(event_handler)));
  }

  // Overload of |Bind| that supports custom behavior on teardown completion via
  // |teardown_observer|. Through helpers that return an |AnyTeardownObserver|,
  // users may link the completion of teardown to the invocation of a callback
  // or the lifecycle of related business objects. See for example
  // |fidl::ObserveTeardown| and |fidl::ShareUntilTeardown|.
  //
  // This overload does not demand taking ownership of |event_handler| by
  // |std::unique_ptr|, hence is suitable when the |event_handler| needs to be
  // managed independently of the client lifetime.
  //
  // See |Bind| above for other behavior aspects of the function.
  void Bind(fidl::ClientEnd<Protocol> client_end, async_dispatcher_t* dispatcher,
            fidl::WireAsyncEventHandler<Protocol>* event_handler,
            fidl::internal::AnyTeardownObserver teardown_observer =
                fidl::internal::AnyTeardownObserver::Noop()) {
    controller_.Bind(std::make_shared<ClientImpl>(), client_end.TakeChannel(), dispatcher,
                     event_handler, std::move(teardown_observer));
  }

  // Overload of |Bind| that omits the |event_handler|, to
  // workaround C++ limitations on default arguments.
  //
  // See |Bind| above for other behavior aspects of the constructor.
  void Bind(fidl::ClientEnd<Protocol> client_end, async_dispatcher_t* dispatcher,
            fidl::internal::AnyTeardownObserver teardown_observer =
                fidl::internal::AnyTeardownObserver::Noop()) {
    Bind(client_end.TakeChannel(), dispatcher, nullptr, std::move(teardown_observer));
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
  // Unbinding can happen automatically via RAII. |WireSharedClient|s will release
  // resources automatically when they are destructed. See also: |~WireSharedClient|.
  void Unbind() { controller_.Unbind(); }

  // Returns another |WireSharedClient| instance sharing the same channel.
  //
  // Prefer to |Clone| only when necessary e.g. extending the lifetime of a
  // |WireSharedClient| to a different scope. Any living clone will prevent the cleanup of
  // the channel, unless one explicitly call |WaitForChannel|.
  WireSharedClient Clone() { return WireSharedClient(*this); }

  // Returns the interface for making outgoing FIDL calls. If the client has
  // been unbound, calls on the interface return error with status
  // |ZX_ERR_CANCELED| and reason |fidl::Reason::kUnbind|.
  //
  // Persisting this pointer to a local variable is discouraged, since that
  // results in unsafe borrows. Always prefer making calls directly via the
  // |fidl::WireSharedClient| reference-counting type. A client may be cloned and handed
  // off through the |Clone| method.
  ClientImpl* operator->() const { return get(); }
  ClientImpl& operator*() const { return *get(); }

 private:
  ClientImpl* get() const { return static_cast<ClientImpl*>(controller_.get()); }

  WireSharedClient(const WireSharedClient& other) noexcept = default;
  WireSharedClient& operator=(const WireSharedClient& other) noexcept = default;

  internal::ClientController controller_;
};

template <typename Protocol, typename AsyncEventHandlerReference>
WireSharedClient(fidl::ClientEnd<Protocol>, async_dispatcher_t*, AsyncEventHandlerReference&&,
                 fidl::internal::AnyTeardownObserver) -> WireSharedClient<Protocol>;

template <typename Protocol, typename AsyncEventHandlerReference>
WireSharedClient(fidl::ClientEnd<Protocol>, async_dispatcher_t*, AsyncEventHandlerReference&&)
    -> WireSharedClient<Protocol>;

template <typename Protocol>
WireSharedClient(fidl::ClientEnd<Protocol>, async_dispatcher_t*) -> WireSharedClient<Protocol>;

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_CLIENT_H_
