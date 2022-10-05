// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_CLIENT_BASE_H_
#define LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_CLIENT_BASE_H_

#include <lib/async/dispatcher.h>
#include <lib/async/time.h>
#include <lib/fidl/cpp/wire/async_binding.h>
#include <lib/fidl/cpp/wire/extract_resource_on_destruction.h>
#include <lib/fidl/cpp/wire/internal/client_continuation.h>
#include <lib/fidl/cpp/wire/internal/client_details.h>
#include <lib/fidl/cpp/wire/internal/intrusive_container/wavl_tree.h>
#include <lib/fidl/cpp/wire/message.h>
#include <lib/fidl/cpp/wire/wire_messaging.h>
#include <lib/fit/traits.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include <memory>
#include <mutex>
#include <optional>

namespace fidl_testing {
// Forward declaration of test helpers to support friend declaration.
class ClientBaseChecker;
class ClientChecker;
}  // namespace fidl_testing

namespace fidl {
namespace internal {

class ClientControlBlock;

// A mixin into |ResponseContext| to handle the asynchronous error delivery
// aspects.
template <typename Derived>
class ResponseContextAsyncErrorTask : private async_task_t {
 public:
  // Try to schedule an |ResponseContext::OnError| as a task on |dispatcher|.
  //
  // If successful, ownership of the context is passed to the |dispatcher| until
  // the task is executed.
  zx_status_t TryAsyncDeliverError(::fidl::Status error, async_dispatcher_t* dispatcher) {
    error_ = error;
    async_task_t* task = this;
    *task = async_task_t{{ASYNC_STATE_INIT},
                         &ResponseContextAsyncErrorTask::AsyncErrorDelivery,
                         async_now(dispatcher)};
    return async_post_task(dispatcher, task);
  }

 private:
  static void AsyncErrorDelivery(async_dispatcher_t* /*unused*/, async_task_t* task,
                                 zx_status_t status) {
    auto* context = static_cast<Derived*>(task);
    auto* self = static_cast<ResponseContextAsyncErrorTask*>(task);
    context->OnError(self->error_);
  }

  ::fidl::Status error_;
};

// |ResponseContext| contains information about an outstanding asynchronous
// method call. It inherits from an intrusive container node so that
// |ClientBase| can track it without requiring heap allocation.
//
// The generated code will define type-specific response contexts e.g.
// |fidl::WireResponseContext<FooMethod>|, that inherits from |ResponseContext|
// and interprets the bytes passed to the |OnRawResult| call appropriately.
// Users should interact with those subclasses; see the lifecycle notes on
// |WireResponseContext|.
//
// NOTE: |ResponseContext| are additionally referenced with a |list_node_t|
// in order to safely iterate over outstanding transactions on |ClientBase|
// destruction, releasing each outstanding response context.
class ResponseContext : public fidl::internal_wavl::WAVLTreeContainable<ResponseContext*>,
                        private list_node_t,
                        private ResponseContextAsyncErrorTask<ResponseContext> {
 public:
  explicit ResponseContext(uint64_t ordinal)
      : fidl::internal_wavl::WAVLTreeContainable<ResponseContext*>(),
        list_node_t(LIST_INITIAL_CLEARED_VALUE),
        ordinal_(ordinal) {}
  virtual ~ResponseContext() = default;

  // |ResponseContext| objects are "pinned" in memory.
  ResponseContext(const ResponseContext& other) = delete;
  ResponseContext& operator=(const ResponseContext& other) = delete;
  ResponseContext(ResponseContext&& other) = delete;
  ResponseContext& operator=(ResponseContext&& other) = delete;

  uint64_t ordinal() const { return ordinal_; }
  zx_txid_t Txid() const { return txid_; }

  // Invoked when a response has been received or an error was detected for this
  // context. |OnRawResult| is allowed to consume the current object.
  //
  // ## If |result| represents a success
  //
  // |result| references the incoming message in encoded form.
  //
  // Ownership of bytes referenced by |result| stays with the caller.
  // The callee should not access the bytes in |result| once this method returns.
  //
  // Ownership of handles referenced by |result| is transferred to the callee.
  //
  // If there was an error decoding |result|, the implementation should return
  // that error as a present |fidl::UnbindInfo|. Otherwise, the implementation
  // should return |std::nullopt|.
  //
  // ## If |result| represents an error
  //
  // An error occurred while processing this FIDL call:
  //
  // - Failed to encode the outgoing request specific to this call.
  // - The peer endpoint was closed.
  // - Error from the |async_dispatcher_t|.
  // - Error from the underlying transport.
  // - The server sent a malformed message.
  // - The user explicitly initiated binding teardown.
  // - The call raced with an external error in the meantime that caused binding
  //   teardown.
  //
  // See |WireResponseContext<FidlMethod>::OnResult| for more details.
  virtual std::optional<fidl::UnbindInfo> OnRawResult(
      ::fidl::IncomingHeaderAndMessage&& result,
      internal::MessageStorageViewBase* storage_view) = 0;

  // A helper around |OnRawResult| to directly notify an error to the context.
  void OnError(::fidl::Status error) {
    OnRawResult(fidl::IncomingHeaderAndMessage::Create(error), nullptr);
  }

 private:
  friend class ResponseContextAsyncErrorTask<ResponseContext>;
  friend class ClientBase;

  // For use with |fidl::internal_wavl::WAVLTree|.
  struct Traits {
    static zx_txid_t GetKey(const ResponseContext& context) { return context.txid_; }
    static bool LessThan(const zx_txid_t& key1, const zx_txid_t& key2) { return key1 < key2; }
    static bool EqualTo(const zx_txid_t& key1, const zx_txid_t& key2) { return key1 == key2; }
  };

  const uint64_t ordinal_;  // Expected ordinal for the response.
  zx_txid_t txid_ = 0;      // Zircon txid of outstanding transaction.
};

}  // namespace internal

// |WireResponseContext| is used to monitor the outcome of an outstanding asynchronous
// |FidlMethod| method call without heap memory allocation. They are used in
// combination with the caller-allocating async API flavors of FIDL clients.
//
// ## Lifecycle
//
// The FIDL runtime has no requirements on how |WireResponseContext|s are
// allocated.
//
// Once a |WireResponseContext| is passed to the client, ownership is
// transferred to the FIDL runtime. Ownership is returned back to the user when
// |OnResult| is invoked. This means that the user must keep the response
// context object alive for the duration of the async method call. |OnResult| is
// guaranteed to be invoked exactly once regardless of success or error.
//
// ## Usage
//
// Subclass |WireResponseContext| and override its |OnResult| method. Example:
//
//     // Lets say we have a `Game` object that loads some required asset
//     // using the `Disk` FIDL protocol and its `Download` method.
//     class Game : public fidl::WireResponseContext<Disk::Download> {
//      public:
//       void LoadGame() {
//         // Passing `this` to the caller-allocating flavor since `Game`
//         // implements the corresponding response context.
//         disk_client_.buffer(arena_)->Download("foo.zip").ThenExactlyOnce(this);
//       }
//
//      private:
//       void OnResult(fidl::WireUnownedResult<Disk::Download>& result) final {
//         if (!result.ok()) {
//           std::cerr << "Downloading failed: " << result.error();
//           return;
//         }
//         // Access the response.
//         fidl::WireResponse<Disk::Download>& response = result.value();
//       }
//
//       fidl::WireClient<Disk> disk_client_;
//       fidl::Arena<> arena_;
//     };
template <typename FidlMethod>
class WireResponseContext : public internal::ResponseContext {
 public:
  WireResponseContext()
      : ::fidl::internal::ResponseContext(internal::WireOrdinal<FidlMethod>::value) {}

  // Invoked when a response has been received or an error was detected for this
  // call.
  //
  // ## If |result| represents a success
  //
  // |result| borrows the decoded response. The implementation may transfer out
  // handles contained in the message, but should not access the bytes in
  // |result| once this method returns.
  //
  // ## If |result| represents an error
  //
  // An error occurred while processing this FIDL call:
  //
  // - Failed to encode the outgoing request specific to this call.
  // - Failed to decode the incoming response specific to this call.
  // - The peer endpoint was closed.
  // - Error from the |async_dispatcher_t|.
  // - Error from the underlying transport.
  // - The server sent a malformed message.
  // - The user explicitly initiated binding teardown.
  // - The call raced with an external error in the meantime that caused binding
  //   teardown.
  //
  // |OnResult| is always invoked asynchronously whether in case of success
  // or error, unless the dispatcher is shut down, in which case it will be
  // called synchronously.
  virtual void OnResult(::fidl::internal::WireUnownedResultType<FidlMethod>& result) = 0;

 private:
  ::std::optional<::fidl::UnbindInfo> OnRawResult(
      ::fidl::IncomingHeaderAndMessage&& msg,
      internal::MessageStorageViewBase* storage_view) final {
    if (unlikely(!msg.ok())) {
      ::fidl::internal::WireUnownedResultType<FidlMethod> result{msg.error()};
      OnResult(result);
      return std::nullopt;
    }
    ::fit::result decoded =
        ::fidl::internal::InplaceDecodeTransactionalResponse<FidlMethod>(std::move(msg));
    ::fidl::Status maybe_error = ::fidl::internal::StatusFromResult(decoded);
    ::fidl::internal::WireUnownedResultType<FidlMethod> result(std::move(decoded), storage_view);
    OnResult(result);
    if (unlikely(!maybe_error.ok())) {
      return ::fidl::UnbindInfo(maybe_error);
    }
    return std::nullopt;
  }
};

namespace internal {

// |ClientBase| sends transactional messages and tracks outstanding replies.
// Different client implementations reference the |ClientBase| to make calls.
//
// It supports multi-threaded asynchronous dispatchers, error handling, and
// thread-safe teardown. Instances are always managed via |std::shared_ptr|.
class ClientBase final : public std::enable_shared_from_this<ClientBase> {
 public:
  // Creates a |ClientBase| by binding to a transport. Notifies
  // |teardown_observer| on binding teardown.
  static std::shared_ptr<ClientBase> Create(
      AnyTransport&& transport, async_dispatcher_t* dispatcher,
      AnyIncomingEventDispatcher&& event_dispatcher, AsyncEventHandler* error_handler,
      fidl::AnyTeardownObserver&& teardown_observer, ThreadingPolicy threading_policy,
      std::weak_ptr<ClientControlBlock> client_object_lifetime);

  // Creates an unbound ClientBase. Only use it with |std::make_shared|.
  ClientBase() = default;
  ~ClientBase() = default;

  // Neither copyable nor movable.
  ClientBase(const ClientBase& other) = delete;
  ClientBase& operator=(const ClientBase& other) = delete;
  ClientBase(ClientBase&& other) = delete;
  ClientBase& operator=(ClientBase&& other) = delete;

  // Asynchronously unbind the client from the dispatcher. |teardown_observer|
  // will be notified on a dispatcher thread.
  void AsyncTeardown();

  // Makes a two-way synchronous call with the transport that is managed by this
  // client.
  //
  // It invokes |sync_call| with a strong reference to the transport to prevent
  // its destruction during a |transport.Call|. The |sync_call| callable must
  // have a return type that could be instantiated with a |fidl::Status| to
  // propagate failures.
  //
  // If the client has been unbound, returns a result type instantiated with
  // a |fidl::Status::Unbound| error.
  //
  // If the client has a valid binding, returns the return value of |sync_call|.
  template <typename Callable>
  auto MakeSyncCallWith(Callable&& sync_call) {
    using ReturnType = typename fit::callable_traits<Callable>::return_type;
    std::shared_ptr<AnyTransport> transport = GetTransport();
    if (!transport) {
      return ReturnType(fidl::Status::Unbound());
    }
    // TODO(fxbug.dev/78906): We should report errors to binding teardown
    // by calling |HandleSendError|. A naive approach of checking the result
    // here doesn't work because the result must be a temporary.
    return sync_call(std::move(transport));
  }

  // Stores the given asynchronous transaction response context, setting the txid field.
  void PrepareAsyncTxn(ResponseContext* context);

  // Forget the transaction associated with the given context. Used when zx_channel_write() fails.
  void ForgetAsyncTxn(ResponseContext* context);

  // Releases all outstanding |ResponseContext|s. Invoked when binding has torn
  // down.
  //
  // |info| is the cause of the binding teardown. If |info| represents an error
  // that is not specific to any single call (e.g. peer closed), all response
  // contexts would be notified of that error.
  void ReleaseResponseContexts(fidl::UnbindInfo info);

  // Sends a two way message.
  //
  // In the process, registers |context| for the corresponding reply and mints
  // a new transaction ID. |message| will be updated with that transaction ID.
  //
  // Errors are notified via |context|.
  void SendTwoWay(fidl::OutgoingMessage& message, ResponseContext* context,
                  fidl::WriteOptions write_options = {});

  // Sends a one way message.
  //
  // |message| will have its transaction ID set to zero.
  //
  // Errors are returned to the caller.
  fidl::Status SendOneWay(::fidl::OutgoingMessage& message, fidl::WriteOptions write_options = {});

  // For debugging.
  size_t GetTransactionCount() {
    std::scoped_lock lock(lock_);
    return contexts_.size();
  }

  // Dispatches a generic incoming message.
  //
  // ## Handling events
  //
  // If the incoming message is an event, the implementation will dispatch it
  // using |event_dispatcher_| which is created when binding the client.
  //
  // ## Message ownership
  //
  // If a matching response handler or event handler is found, |msg| is then
  // consumed, regardless of decoding error. Otherwise, |msg| is not consumed.
  //
  // ## Return value
  //
  // If errors occur during dispatching, the function will return an
  // |UnbindInfo| describing the error. Otherwise, it will return
  // |std::nullopt|.
  std::optional<UnbindInfo> Dispatch(fidl::IncomingHeaderAndMessage& msg,
                                     internal::MessageStorageViewBase* storage_view);

  // Returns a weak pointer representing the lifetime of client objects exposed
  // to the user, e.g. |fidl::WireClient|.
  //
  // When the weak pointer is expired, it indicates that the corresponding
  // client objects have destructed.
  const std::weak_ptr<ClientControlBlock>& client_object_lifetime() const {
    return client_object_lifetime_;
  }

 private:
  void Bind(AnyTransport&& transport, async_dispatcher_t* dispatcher,
            AnyIncomingEventDispatcher&& event_dispatcher, AsyncEventHandler* error_handler,
            fidl::AnyTeardownObserver&& teardown_observer, ThreadingPolicy threading_policy,
            std::weak_ptr<ClientControlBlock> client_object_lifetime);

  // Handles errors in sending one-way or two-way FIDL requests. This may lead
  // to binding teardown.
  void HandleSendError(fidl::Status error);

  // Try to asynchronously notify |context| of the |error|. If not possible
  // (e.g. dispatcher shutting down), notify it synchronously as a last resort.
  void TryAsyncDeliverError(::fidl::Status error, ResponseContext* context);

  std::shared_ptr<AnyTransport> GetTransport() {
    if (auto binding = binding_.lock()) {
      return binding->GetTransport();
    }
    return nullptr;
  }

  // Allow unit tests to peek into the internals of this class.
  friend class ::fidl_testing::ClientBaseChecker;

  // Weak reference to the internal binding state.
  std::weak_ptr<AsyncClientBinding> binding_;

  std::weak_ptr<ClientControlBlock> client_object_lifetime_;

  // The dispatcher that is monitoring FIDL messages.
  async_dispatcher_t* dispatcher_ = nullptr;

  // The event dispatcher to decode and notify FIDL events.
  AnyIncomingEventDispatcher event_dispatcher_;

  // State for tracking outstanding transactions.
  std::mutex lock_;

  // The base node of an intrusive container of ResponseContexts corresponding to outstanding
  // asynchronous transactions.
  fidl::internal_wavl::WAVLTree<zx_txid_t, ResponseContext*, ResponseContext::Traits> contexts_
      __TA_GUARDED(lock_);

  // Mirror list used to safely invoke OnError() on outstanding ResponseContexts in ~ClientBase().
  list_node_t delete_list_ __TA_GUARDED(lock_) = LIST_INITIAL_VALUE(delete_list_);

  // Value used to compute the next txid.
  zx_txid_t txid_base_ __TA_GUARDED(lock_) = 0;
};

// |ControlBlock| controls the lifecycle of a client binding, such that
// teardown will only happen after all clones of a |Client| managing
// the same transport goes out of scope.
//
// Specifically, all clones of a |Client| will share the same |ControlBlock|
// instance, which in turn references the |ClientBase|, and is responsible
// for its teardown via RAII.
class ClientControlBlock final : public std::enable_shared_from_this<ClientControlBlock> {
 public:
  explicit ClientControlBlock(std::shared_ptr<ClientBase> client)
      : client_impl_(std::move(client)) {}

  ClientControlBlock(const ClientControlBlock&) noexcept = delete;
  ClientControlBlock& operator=(const ClientControlBlock&) noexcept = delete;
  ClientControlBlock(ClientControlBlock&&) noexcept = default;
  ClientControlBlock& operator=(ClientControlBlock&&) noexcept = default;

  // Triggers teardown, which will cause any strong references to the
  // |ClientBase| to be released.
  ~ClientControlBlock() {
    if (client_impl_) {
      client_impl_->AsyncTeardown();
    }
  }

 private:
  std::shared_ptr<ClientBase> client_impl_;
};

// |ClientController| manages the lifetime of a |ClientBase| instance.
//
// |ClientBase|s are created when binding a client endpoint to a message
// dispatcher, via |Bind|. The destruction of |ClientBase|s is initiated when
// this |ClientController| class destructs, or when |Unbind| is explicitly
// invoked.
class ClientController {
 public:
  ClientController() = default;
  ~ClientController() = default;

  ClientController(ClientController&& other) noexcept = default;
  ClientController& operator=(ClientController&& other) noexcept = default;
  ClientController(const ClientController& other) = default;
  ClientController& operator=(const ClientController& other) = default;

  // Creates a |ClientBase| and binds it to the |dispatcher| and |client_end|,
  // starts managing its lifetime.
  //
  // It is an error to call |Bind| more than once on the same controller.
  void Bind(AnyTransport client_end, async_dispatcher_t* dispatcher,
            AnyIncomingEventDispatcher&& event_dispatcher, AsyncEventHandler* error_handler,
            fidl::AnyTeardownObserver&& teardown_observer, ThreadingPolicy threading_policy);

  // Begins to unbind the transport from the dispatcher. In particular, it
  // triggers the asynchronous destruction of the bound |ClientBase|. May be
  // called from any thread. If provided, the teardown observer is notified
  // asynchronously on a dispatcher thread.
  //
  // |Bind| must have been called before this.
  void Unbind();

  bool is_valid() const { return static_cast<bool>(client_impl_); }
  explicit operator bool() const { return is_valid(); }

  ClientBase& get() const {
    ZX_ASSERT(is_valid());
    return *client_impl_;
  }

 private:
  // Allow unit tests to peek into the internals of this class.
  friend ::fidl_testing::ClientChecker;

  std::shared_ptr<ClientBase> client_impl_;
  std::shared_ptr<ClientControlBlock> control_;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_WIRE_INCLUDE_LIB_FIDL_CPP_WIRE_CLIENT_BASE_H_
