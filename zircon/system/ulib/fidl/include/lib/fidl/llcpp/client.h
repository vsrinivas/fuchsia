// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_CLIENT_H_
#define LIB_FIDL_LLCPP_CLIENT_H_

#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl/llcpp/client_end.h>
#include <lib/fidl/llcpp/wire_messaging.h>

namespace fidl {

namespace internal {

// |ControlBlock| controls the lifecycle of |ClientImpl|, such that
// unbinding will only happen after all clones of a |Client| managing
// the same channel goes out of scope.
//
// Specifically, all clones of a |Client| will share the same |ControlBlock|
// instance, which in turn references the |ClientImpl|, and is responsible
// for its unbinding via RAII.
template <typename Protocol>
class ControlBlock final {
  using ClientImpl = fidl::internal::WireClientImpl<Protocol>;

 public:
  explicit ControlBlock(std::shared_ptr<ClientImpl> client) : client_(std::move(client)) {}

  // Triggers unbinding, which will cause any strong references to the
  // |ClientBase| to be released.
  ~ControlBlock() {
    if (client_) {
      client_->ClientBase::Unbind();
    }
  }

 private:
  std::shared_ptr<ClientImpl> client_;
};

}  // namespace internal

// This class wraps the LLCPP thread-safe client. It provides methods for
// binding a channel's client end to a dispatcher, unbinding the channel, and
// recovering the channel. Generated FIDL APIs are accessed by 'dereferencing'
// the Client:
//
//     // Creates a client that speaks over |client_end|, on the |my_dispatcher| dispatcher.
//     fidl::Client client(std::move(client_end), my_dispatcher);
//
//     // Call the |Foo| method asynchronously, passing in a callback that will be
//     // invoked on a dispatcher thread when the server response arrives.
//     auto status = client->Foo(args, [] (Result result) {});
//
// This class itself is NOT thread-safe. The user is responsible for ensuring
// that |Bind| is serialized with respect to other calls on Client APIs. Also,
// |Bind| must have been called before any calls are made to other APIs, if the
// client was default constructed.
//
// ## Lifecycle
//
// A |Client| may either be constructed already-bound, or default-constructed first
// and bound to a channel later via |Bind|.
//
// The user may then invoke asynchronous or synchronous FIDL methods on the client.
//
// When the user is done with the |Client|, they can simply let it go out of scope,
// and unbinding and resource clean-up will happen automatically.
//
// One may |Clone| a |Client| to obtain another |Client| referencing the same channel.
// In that case, the unbinding will happen when all related |Client|s go out of scope.
//
// |Unbind| may be called on a |Client| to explicitly initiate unbinding. After
// unbinding finishes, the client will not monitor new messages on the channel, and
// further calls on the same client will fail. As a result, asynchronous calls that
// are in-flight will be forgotten, and the response callbacks provided by the user
// will be dropped. Events that have arrived on the channel, but are queued _after_
// the unbind operation on the dispatcher, will also be discarded.
//
// |WaitForChannel| is another way to trigger unbinding, with the bonus of recovering
// the channel as the return value. Care must be taken when using this function,
// as it will be waiting for any synchronous calls to finish, and will forget about
// any in-progress asynchronous calls.
//
// TODO(fxbug.dev/68742): We may want to also wait for asynchronous calls, or panic
// when there are in-flight asynchronous calls.
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
  // atomically during construction. Use this default constructor only when
  // the client must be constructed first before a channel could be obtained
  // (for example, if the client is an instance variable).
  Client() = default;

  // Returns if the |Client| is initialized.
  bool is_valid() const { return static_cast<bool>(client_); }
  explicit operator bool() const { return is_valid(); }

  // If the current |Client| is the last instance controlling the current
  // connection, the destructor of this |Client| will trigger unbinding,
  // which will cause any strong references to the |ClientBase| to be released.
  //
  // When the last |Client| destructs:
  // - The channel will be closed.
  // - Pointers obtained via |get| will be invalidated.
  // - Unbinding will happen, implying:
  //   * In-progress calls will be forgotten. Async callbacks will be dropped.
  //   * The |Unbound| callback in the |event_handler| will be invoked,
  //     if one was specified when creating the client, on a dispatcher thread.
  //
  // See also: |Unbind|.
  ~Client() = default;

  // |fidl::Client|s can be safely moved without affecting any in-progress
  // operations. Note that calling methods on a client should be serialized
  // with respect to operations that consume the client, such as moving it
  // or destroying it.
  Client(Client&& other) noexcept = default;
  Client& operator=(Client&& other) noexcept = default;
  Client(const Client& other) = delete;
  Client& operator=(const Client& other) = delete;

  // Bind the |client_end| endpoint to the dispatcher. If Client is already
  // initialized, destroys the previous binding, releasing its channel.
  //
  // It is a logic error to invoke |Bind| on a dispatcher that is
  // shutting down or already shut down. Doing so will result in a panic.
  //
  // When other error occurs during binding, the |event_handler->Unbound| handler
  // will be asynchronously invoked with the reason, if specified.
  //
  // Re-binding a |Client| to a different channel is equivalent to replacing
  // the |Client| with a new instance.
  void Bind(fidl::ClientEnd<Protocol> client_end, async_dispatcher_t* dispatcher,
            std::shared_ptr<fidl::WireAsyncEventHandler<Protocol>> event_handler = nullptr) {
    if (client_) {
      // This way, the current |Client| will effectively start from a clean slate.
      // If this |Client| were the only instance for that particular channel,
      // destroying |control_| would trigger unbinding automatically.
      control_.reset();
      client_.reset();
    }

    // Cannot use |std::make_shared| because the |ClientImpl| constructor is private.
    client_.reset(new ClientImpl());
    client_->Bind(std::reinterpret_pointer_cast<internal::ClientBase>(client_),
                  client_end.TakeChannel(), dispatcher, std::move(event_handler));
    control_ = std::make_shared<internal::ControlBlock<Protocol>>(client_);
  }

  // Begins to unbind the channel from the dispatcher. May be called from any
  // thread. If provided, the |fidl::WireAsyncEventHandler<Protocol>::Unbound| is invoked
  // asynchronously on a dispatcher thread.
  //
  // NOTE: |Bind| must have been called before this.
  //
  // WARNING: While it is safe to invoke Unbind() from any thread, it is unsafe
  // to wait on the |fidl::WireAsyncEventHandler<Protocol>::Unbound| from a dispatcher
  // thread, as that will likely deadlock.
  //
  // Unbinding can happen automatically via RAII. |Client|s will release
  // resources automatically when they are destructed. See also: |~Client|.
  void Unbind() {
    ZX_ASSERT(client_);
    control_.reset();
    client_->ClientBase::Unbind();
  }

  // Returns another |Client| instance sharing the same channel.
  //
  // Prefer to |Clone| only when necessary e.g. extending the lifetime of a
  // |Client| to a different scope. Any living clone will prevent the cleanup of
  // the channel, unless one explicitly call |WaitForChannel|.
  fidl::Client<Protocol> Clone() { return fidl::Client<Protocol>(client_, control_); }

  // Returns the underlying channel. Unbinds from the dispatcher if required.
  //
  // NOTE: |Bind| must have been called before this.
  //
  // WARNING: This is a blocking call. It waits for completion of dispatcher unbind and of any
  // channel operations, including synchronous calls which may block indefinitely.
  fidl::ClientEnd<Protocol> WaitForChannel() {
    ZX_ASSERT(client_);
    control_.reset();
    return fidl::ClientEnd<Protocol>(client_->WaitForChannel());
  }

  // Returns the interface for making outgoing FIDL calls. If the client
  // has been unbound, calls on the interface return error with status
  // |ZX_ERR_CANCELED|.
  //
  // Persisting this pointer to a local variable is discouraged, since that
  // results in unsafe borrows. Always prefer making calls directly via the
  // |fidl::Client| or |fidl::WireClient| reference-counting type. A client
  // may be cloned and handed off through the |Clone()| method.
  ClientImpl* operator->() const { return get(); }
  ClientImpl& operator*() const { return *get(); }

 private:
  ClientImpl* get() const { return client_.get(); }

  // Used to clone a |Client|.
  Client(std::shared_ptr<ClientImpl> client,
         std::shared_ptr<internal::ControlBlock<Protocol>> control)
      : client_(std::move(client)), control_(std::move(control)) {}

  std::shared_ptr<ClientImpl> client_;
  std::shared_ptr<internal::ControlBlock<Protocol>> control_;
};

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_CLIENT_H_
