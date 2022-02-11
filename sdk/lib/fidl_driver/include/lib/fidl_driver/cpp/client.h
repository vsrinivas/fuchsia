// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_CLIENT_H_
#define LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_CLIENT_H_

#include <lib/fdf/cpp/arena.h>
#include <lib/fdf/dispatcher.h>
#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl_driver/cpp/internal/client_details.h>
#include <lib/fidl_driver/cpp/transport.h>
#include <lib/fidl_driver/cpp/wire_messaging_declarations.h>

//
// Maintainer's note: when updating the documentation and function signatures
// below, please make similar updates to the one in
// //zircon/system/ulib/fidl/include/lib/fidl/llcpp/client.h.
//
// |fdf::WireClient| is the driver counterpart to |fidl::WireClient|, augmented
// to work with driver runtime objects, and similar for |fdf::WireSharedClient|
// and |fdf::WireSharedClient|. There is no hard-and-fast rule as to their
// interface requirements, but methods that don't concern themselves with driver
// specifics should generally stay identical between `fdf` and `fidl`.
//

namespace fdf {

// |fdf::WireClient| is a client for sending and receiving FIDL wire messages
// over the driver transport. It exposes similar looking interfaces as
// |fidl::WireClient|, but has driver-specific concepts such as arenas and
// driver dispatchers.
//
// See |fidl::WireClient| for lifecycle notes, which also apply to
// |fdf::WireClient|.
//
// ## Thread safety
//
// |WireClient| provides an easier to use API in exchange of a more restrictive
// threading model:
//
// - There must only ever be one thread executing asynchronous operations for
//   the provided |fdf_dispatcher_t|, termed "the dispatcher thread".
// - The client must be bound on the dispatcher thread.
// - The client must be destroyed on the dispatcher thread.
// - FIDL method calls may be made on other threads, but the response is always
//   delivered on the dispatcher thread, as are event callbacks.
//
// The above rules are checked in debug builds at run-time. In short, the client
// is local to a thread.
//
// Note that FIDL method calls must be synchronized with operations that consume
// or mutate the |WireClient| itself:
//
// - Assigning a new value to the |WireClient| variable.
// - Moving the |WireClient| to a different location.
// - Destroying the |WireClient|.
//
// See
// https://fuchsia.dev/fuchsia-src/development/languages/fidl/guides/llcpp-threading
// for thread safety notes on |fidl::WireClient|, which also largely apply to
// |fdf::WireClient|.
//
// TODO(fxbug.dev/90958): support hopping threads in the driver async dispatcher
// as long as the dispatcher is SYNCHRONIZED.
template <typename Protocol>
class WireClient {
 public:
  // Create an initialized client which manages the binding of the client end of
  // a channel to a dispatcher, as if that client had been default-constructed
  // then later bound to that endpoint via |Bind|.
  //
  // It is a logic error to use a dispatcher that is shutting down or already
  // shut down. Doing so will result in a panic.
  //
  // If any other error occurs during initialization, the
  // |event_handler->on_fidl_error| handler will be invoked asynchronously with
  // the reason, if specified.
  WireClient(fdf::ClientEnd<Protocol> client_end, fdf_dispatcher_t* dispatcher,
             fdf::WireAsyncEventHandler<Protocol>* event_handler = nullptr) {
    Bind(std::move(client_end), dispatcher, event_handler);
  }

  // Create an uninitialized client. The client may then be bound to an endpoint
  // later via |Bind|.
  //
  // Prefer using the constructor overload that binds the client to a channel
  // atomically during construction. Use this default constructor only when the
  // client must be constructed first before a channel could be obtained (for
  // example, if the client is an instance variable).
  WireClient() = default;

  // Returns if the |WireClient| is initialized.
  bool is_valid() const { return controller_.is_valid(); }
  explicit operator bool() const { return is_valid(); }

  // The destructor of |WireClient| will initiate binding teardown.
  //
  // When the client destructs:
  // - The channel will be closed.
  // - Pointers obtained via |get| will be invalidated.
  // - Binding teardown will happen, implying:
  //   * In-progress calls will be forgotten. Async callbacks will be dropped.
  ~WireClient() = default;

  // |WireClient|s can be safely moved without affecting any in-flight FIDL
  // method calls. Note that calling methods on a client should be serialized
  // with respect to operations that consume the client, such as moving it or
  // destroying it.
  WireClient(WireClient&& other) noexcept = default;
  WireClient& operator=(WireClient&& other) noexcept = default;

  // Initializes the client by binding the |client_end| endpoint to the
  // dispatcher.
  //
  // It is a logic error to invoke |Bind| on a dispatcher that is shutting down
  // or already shut down. Doing so will result in a panic.
  //
  // When other errors occur during binding, the |event_handler->on_fidl_error|
  // handler will be asynchronously invoked with the reason, if specified.
  //
  // It is not allowed to call |Bind| on an initialized client. To rebind a
  // |WireClient| to a different endpoint, simply replace the |WireClient|
  // variable with a new instance.
  void Bind(fdf::ClientEnd<Protocol> client_end, fdf_dispatcher_t* dispatcher,
            fdf::WireAsyncEventHandler<Protocol>* event_handler = nullptr) {
    controller_.Bind(fidl::internal::MakeAnyTransport(client_end.TakeHandle()),
                     fdf_dispatcher_get_async_dispatcher(dispatcher),
                     fidl::internal::MakeAnyEventDispatcher(event_handler),
                     fidl::AnyTeardownObserver::Noop(),
                     fidl::internal::ThreadingPolicy::kCreateAndTeardownFromDispatcherThread);
  }

  // Returns an interface for making FIDL calls, using the provided |arena| to
  // allocate buffers necessary for each call. Requests will live on the arena.
  // Responses on the other hand live on the arena passed along with the
  // response, which may or may not be the same arena as the request.
  //
  // Note that there are a few overloads for each two-way method:
  //
  // * Response callbacks:
  //
  //   void MyMethod(args, [] (fidl::WireResponse<MyMethod>*) { ... });
  //
  //   The callback is only invoked when the corresponding response is received.
  //
  // * Result callbacks:
  //
  //   void MyMethod(args, [] (fidl::WireUnownedResult<MyMethod>&) { ... });
  //
  //   The callback is invoked exactly once, with either a response or an error.
  //
  // * Request context:
  //
  //   void MyMethod(args, fidl::WireRequestContext<MyMethod>* context);
  //
  //   This overload is suitable when one needs complete control over memory
  //   allocation. Instead of implicitly heap allocating the necessary
  //   bookkeeping for in-flight operations, the methods take a raw pointer to a
  //   |fidl::WireResponseContext<FidlMethod>|, which may be allocated via any
  //   means as long as it outlives the duration of this async FIDL call. Refer
  //   to documentation on the response context.
  //
  // ## Lifecycle
  //
  // The returned object borrows from this object, hence must not outlive
  // the client object. The return object borrows the arena, hence must not
  // outlive the arena.
  //
  // The returned object may be briefly persisted for use over multiple calls:
  //
  //     fdf::Arena my_arena = /* create the arena */;
  //     fdf::WireClient client(std::move(client_end), some_dispatcher);
  //     auto buffered = client.buffer(my_arena);
  //     buffered->FooMethod(args, foo_response_context);
  //     buffered->BarMethod(args, bar_response_context);
  //     ...
  //
  // In this situation, those calls will all use the initially provided arena
  // to allocate their message buffers.
  auto buffer(const fdf::Arena& arena) const {
    ZX_ASSERT(is_valid());
    return fidl::internal::Arrow<fidl::internal::WireWeakAsyncBufferClientImpl<Protocol>>(&get(),
                                                                                          arena);
  }

 private:
  fidl::internal::ClientBase& get() const { return controller_.get(); }

  WireClient(const WireClient& other) noexcept = delete;
  WireClient& operator=(const WireClient& other) noexcept = delete;

  fidl::internal::ClientController controller_;
};

// |fdf::WireSharedClient| is a client for sending and receiving FIDL wire messages
// over the driver transport. It exposes similar looking interfaces as
// |fidl::WireSharedClient|, but has driver-specific concepts such as arenas and
// driver dispatchers.
//
// |WireSharedClient| is suitable for systems with less defined threading
// guarantees, by providing the building blocks to implement a two-phase
// asynchronous shutdown pattern.
//
// In addition, |WireSharedClient| supports cloning multiple instances sharing
// the same underlying endpoint.
//
// ## Thread safety
//
// FIDL method calls on this class are thread-safe. |AsyncTeardown| and |Clone|
// are also thread-safe, and may be invoked in parallel with FIDL method calls.
// However, those operations must be synchronized with operations that consume
// or mutate the |WireSharedClient| itself:
//
// - Assigning a new value to the |WireSharedClient| variable.
// - Moving the |WireSharedClient| to a different location.
// - Destroying the |WireSharedClient| variable.
//
// See
// https://fuchsia.dev/fuchsia-src/development/languages/fidl/guides/llcpp-threading
// for thread safety notes on |fidl::WireSharedClient|, which also largely apply
// to |fdf::WireSharedClient|.
template <typename Protocol>
class WireSharedClient final {
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
  WireSharedClient(fdf::ClientEnd<Protocol> client_end, fdf_dispatcher_t* dispatcher,
                   std::unique_ptr<fdf::WireAsyncEventHandler<Protocol>> event_handler) {
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
  WireSharedClient(
      fdf::ClientEnd<Protocol> client_end, fdf_dispatcher_t* dispatcher,
      fdf::WireAsyncEventHandler<Protocol>* event_handler,
      fidl::AnyTeardownObserver teardown_observer = fidl::AnyTeardownObserver::Noop()) {
    Bind(std::move(client_end), dispatcher, event_handler, std::move(teardown_observer));
  }

  // Overload of |WireSharedClient| that omits the |event_handler|, to
  // workaround C++ limitations on default arguments.
  //
  // See |WireSharedClient| above for other behavior aspects of the constructor.
  WireSharedClient(
      fdf::ClientEnd<Protocol> client_end, fdf_dispatcher_t* dispatcher,
      fidl::AnyTeardownObserver teardown_observer = fidl::AnyTeardownObserver::Noop()) {
    Bind(std::move(client_end), dispatcher, nullptr, std::move(teardown_observer));
  }

  // Creates an uninitialized |WireSharedClient|.
  //
  // Prefer using the constructor overload that binds the client to a channel
  // atomically during construction. Use this default constructor only when the
  // client must be constructed first before a channel could be obtained (for
  // example, if the client is an instance variable).
  WireSharedClient() = default;

  // Returns if the |WireSharedClient| is initialized.
  bool is_valid() const { return controller_.is_valid(); }
  explicit operator bool() const { return is_valid(); }

  // If the current |WireSharedClient| is the last instance controlling the
  // current connection, the destructor of this |WireSharedClient| will trigger
  // teardown.
  //
  // When the last |WireSharedClient| destructs:
  // - The channel will be closed.
  // - Pointers obtained via |get| will be invalidated.
  // - Teardown will be initiated. See the **Lifecycle** section from the
  //   class documentation of |fidl::WireClient|.
  //
  // See also: |AsyncTeardown|.
  ~WireSharedClient() = default;

  // |fidl::WireSharedClient|s can be safely moved without affecting any in-progress
  // operations. Note that calling methods on a client should be serialized with
  // respect to operations that consume the client, such as moving it or
  // destroying it.
  WireSharedClient(WireSharedClient&& other) noexcept = default;
  WireSharedClient& operator=(WireSharedClient&& other) noexcept = default;

  // Initializes the client by binding the |client_end| endpoint to the dispatcher.
  //
  // It is a logic error to invoke |Bind| on a dispatcher that is shutting down
  // or already shut down. Doing so will result in a panic.
  //
  // It is not allowed to call |Bind| on an initialized client. To rebind a
  // |WireSharedClient| to a different endpoint, simply replace the
  // |WireSharedClient| variable with a new instance.
  //
  // When other error occurs during binding, the |event_handler->on_fidl_error|
  // handler will be asynchronously invoked with the reason, if specified.
  //
  // |event_handler| will be destroyed when teardown completes.
  void Bind(fdf::ClientEnd<Protocol> client_end, fdf_dispatcher_t* dispatcher,
            std::unique_ptr<fdf::WireAsyncEventHandler<Protocol>> event_handler) {
    auto event_handler_raw = event_handler.get();
    Bind(std::move(client_end), dispatcher, event_handler_raw,
         fidl::AnyTeardownObserver::ByOwning(std::move(event_handler)));
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
  void Bind(fdf::ClientEnd<Protocol> client_end, fdf_dispatcher_t* dispatcher,
            fdf::WireAsyncEventHandler<Protocol>* event_handler,
            fidl::AnyTeardownObserver teardown_observer = fidl::AnyTeardownObserver::Noop()) {
    controller_.Bind(fidl::internal::MakeAnyTransport(client_end.TakeHandle()),
                     fdf_dispatcher_get_async_dispatcher(dispatcher),
                     fidl::internal::MakeAnyEventDispatcher(event_handler),
                     std::move(teardown_observer),
                     fidl::internal::ThreadingPolicy::kCreateAndTeardownFromAnyThread);
  }

  // Overload of |Bind| that omits the |event_handler|, to
  // workaround C++ limitations on default arguments.
  //
  // See |Bind| above for other behavior aspects of the constructor.
  void Bind(fdf::ClientEnd<Protocol> client_end, fdf_dispatcher_t* dispatcher,
            fidl::AnyTeardownObserver teardown_observer = fidl::AnyTeardownObserver::Noop()) {
    Bind(std::move(client_end), dispatcher, nullptr, std::move(teardown_observer));
  }

  // Initiates asynchronous teardown of the bindings. See the **Lifecycle**
  // section from |fidl::WireSharedClient|.
  //
  // |Bind| must have been called before this.
  //
  // While it is safe to invoke |AsyncTeardown| from any thread, it is unsafe to
  // wait for teardown to complete from a dispatcher thread, as that will likely
  // deadlock.
  void AsyncTeardown() { controller_.Unbind(); }

  // Returns another |WireSharedClient| instance sharing the same channel.
  //
  // Prefer to |Clone| only when necessary e.g. extending the lifetime of a
  // |SharedClient| to a different scope. Any clone will prevent the cleanup
  // of the channel while the binding is alive.
  WireSharedClient Clone() { return WireSharedClient(*this); }

  // Returns a veneer object which exposes the caller-allocating API, using
  // the provided |resource| to allocate buffers necessary for each call.
  // See documentation on |WireClient::buffer| for detailed behavior.
  //
  // TODO(fxbug.dev/91107): Consider taking |const fdf::Arena&| or similar.
  auto buffer(const fdf::Arena& arena) const {
    ZX_ASSERT(is_valid());
    return fidl::internal::Arrow<fidl::internal::WireWeakAsyncBufferClientImpl<Protocol>>(&get(),
                                                                                          arena);
  }

 private:
  // Allow unit tests to peek into the internals of this class.
  friend ::fidl_testing::ClientChecker;

  fidl::internal::ClientBase& get() const { return controller_.get(); }

  WireSharedClient(const WireSharedClient& other) noexcept = default;
  WireSharedClient& operator=(const WireSharedClient& other) noexcept = default;

  fidl::internal::ClientController controller_;
};

}  // namespace fdf

#endif  // LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_CLIENT_H_
