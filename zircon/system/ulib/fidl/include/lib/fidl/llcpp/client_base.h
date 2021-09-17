// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_CLIENT_BASE_H_
#define LIB_FIDL_LLCPP_CLIENT_BASE_H_

#include <lib/async/dispatcher.h>
#include <lib/async/time.h>
#include <lib/fidl/llcpp/async_binding.h>
#include <lib/fidl/llcpp/extract_resource_on_destruction.h>
#include <lib/fidl/llcpp/internal/client_details.h>
#include <lib/fidl/llcpp/internal/intrusive_container/wavl_tree.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fit/traits.h>
#include <lib/stdcompat/optional.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include <memory>
#include <mutex>

namespace fidl_testing {
// Forward declaration of test helpers to support friend declaration.
class ClientBaseChecker;
}  // namespace fidl_testing

namespace fidl {
namespace internal {

// A mixin into |ResponseContext| to handle the asynchronous error delivery
// aspects.
template <typename Derived>
class ResponseContextAsyncErrorTask : private async_task_t {
 public:
  // Try to schedule an |ResponseContext::OnError| as a task on |dispatcher|.
  //
  // If successful, ownership of the context is passed to the |dispatcher| until
  // the task is executed.
  zx_status_t TryAsyncDeliverError(::fidl::Result error, async_dispatcher_t* dispatcher) {
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

  ::fidl::Result error_;
};

// |ResponseContext| contains information about an outstanding asynchronous
// method call. It inherits from an intrusive container node so that
// |ClientBase| can track it without requiring heap allocation.
//
// The generated code will define type-specific response contexts e.g.
// `FooMethodResponseContext`, that inherits from |ResponseContext| and
// interprets the bytes passed to the |OnReply| call appropriately.
// Users should interact with those subclasses; the notes here on lifecycle
// apply to those subclasses.
//
// ## Lifecycle
//
// The bindings runtime has no opinions about how |ResponseContext|s are
// allocated.
//
// Once a |ResponseContext| is passed to the bindings runtime, ownership is
// transferred to the bindings (in particular, the |ClientBase| object).
// Ownership is returned back to the caller when |OnRawReply|is invoked. This
// means that the user or generated code must keep the response context object
// alive for the duration of the async method call.
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
  // ## If |result| respresents a success
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
  // should return |cpp17::nullopt|.
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
  // |OnRawResult| is always invoked asynchronously whether in case of success
  // or error, unless the dispatcher is shut down, in which case it will be
  // called synchronously.
  virtual cpp17::optional<fidl::UnbindInfo> OnRawResult(::fidl::IncomingMessage&& result) = 0;

  // A helper around |OnRawResult| to directly notify an error to the context.
  void OnError(::fidl::Result error) { OnRawResult(::fidl::IncomingMessage(error)); }

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

// Base LLCPP client class supporting use with a multithreaded asynchronous dispatcher, safe error
// handling and teardown, and asynchronous transaction tracking. Users should not directly interact
// with this class. |ClientBase| objects must be managed via std::shared_ptr.
class ClientBase {
 protected:
  // Creates an unbound ClientBase. Bind() must be called before any other APIs are invoked.
  ClientBase() = default;
  virtual ~ClientBase() = default;

  // Neither copyable nor movable.
  ClientBase(const ClientBase& other) = delete;
  ClientBase& operator=(const ClientBase& other) = delete;
  ClientBase(ClientBase&& other) = delete;
  ClientBase& operator=(ClientBase&& other) = delete;

  // Bind the channel to the dispatcher. Notifies |teardown_observer| on binding
  // teardown. NOTE: This is not thread-safe and must be called exactly once,
  // before any other APIs.
  void Bind(std::shared_ptr<ClientBase> client, zx::channel channel, async_dispatcher_t* dispatcher,
            AsyncEventHandler* event_handler, fidl::AnyTeardownObserver&& teardown_observer,
            ThreadingPolicy threading_policy);

  // Asynchronously unbind the client from the dispatcher. |teardown_observer|
  // will be notified on a dispatcher thread.
  void AsyncTeardown();

  // Makes a two-way synchronous call with the channel that is managed by this
  // client.
  //
  // It invokes |sync_call| with a strong reference to the channel to prevent
  // its destruction during a |zx_channel_call|. The |sync_call| callable must
  // have a return type that could be instantiated with a |fidl::Result| to
  // propagate failures.
  //
  // If the client has been unbound, returns a result type instantiated with
  // a |fidl::Result::Unbound| error.
  //
  // If the client has a valid binding, returns the return value of |sync_call|.
  template <typename Callable>
  auto MakeSyncCallWith(Callable&& sync_call) {
    using ReturnType = typename fit::callable_traits<Callable>::return_type;
    std::shared_ptr<zx::channel> channel = GetChannel();
    if (!channel) {
      return ReturnType(fidl::Result::Unbound());
    }
    // TODO(fxbug.dev/78906): We should report errors to binding teardown
    // by calling |HandleSendError|. A naive approach of checking the result
    // here doesn't work because the result must be a temporary.
    return sync_call(std::move(channel));
  }

 public:
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
  void SendTwoWay(::fidl::OutgoingMessage& message, ResponseContext* context);

  // Sends a one way message.
  //
  // |message| will have its transaction ID set to zero.
  //
  // Errors are returned to the caller.
  fidl::Result SendOneWay(::fidl::OutgoingMessage& message);

  // For debugging.
  size_t GetTransactionCount() {
    std::scoped_lock lock(lock_);
    return contexts_.size();
  }

  // Dispatches a generic incoming message.
  //
  // ## Handling events
  //
  // If the incoming message is an event, the implementation should dispatch it
  // using the optional |maybe_event_handler|.
  //
  // If |maybe_event_handler| is null, the implementation should perform all the
  // checks that the message is valid and a recognized event, but not
  // actually invoke the event handler.
  //
  // If |maybe_event_handler| is present, it should point to a event handler
  // subclass which corresponds to the protocol of |ClientImpl|. This constraint
  // is typically enforced when creating the client.
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
  std::optional<UnbindInfo> Dispatch(fidl::IncomingMessage& msg,
                                     AsyncEventHandler* maybe_event_handler);

  // Dispatches an incoming event.
  //
  // This should be implemented by the generated messaging layer.
  //
  // ## Handling events
  //
  // If |maybe_event_handler| is null, the implementation should perform all the
  // checks that the message is valid and a recognized event, but not
  // actually invoke the event handler.
  //
  // If |maybe_event_handler| is present, it should point to a event handler
  // subclass which corresponds to the protocol of |ClientImpl|. This constraint
  // is typically enforced when creating the client.
  //
  // ## Message ownership
  //
  // If a matching event handler is found, |msg| is then consumed, regardless of
  // decoding error. Otherwise, |msg| is not consumed.
  //
  // ## Return value
  //
  // If errors occur during dispatching, the function will return an
  // |UnbindInfo| describing the error. Otherwise, it will return
  // |std::nullopt|.
  virtual std::optional<UnbindInfo> DispatchEvent(fidl::IncomingMessage& msg,
                                                  AsyncEventHandler* maybe_event_handler) = 0;

 private:
  // Handles errors in sending one-way or two-way FIDL requests. This may lead
  // to binding teardown.
  void HandleSendError(fidl::Result error);

  // Try to asynchronously notify |context| of the |error|. If not possible
  // (e.g. dispatcher shutting down), notify it synchronously as a last resort.
  void TryAsyncDeliverError(::fidl::Result error, ResponseContext* context);

  std::shared_ptr<zx::channel> GetChannel() {
    if (auto binding = binding_.lock()) {
      return binding->GetChannel();
    }
    return nullptr;
  }

  // Allow unit tests to peek into the internals of this class.
  friend class ::fidl_testing::ClientBaseChecker;

  // TODO(fxbug.dev/82085): Instead of protecting methods and adding friends,
  // we should use composition over inheriting from ClientBase.
  friend class ClientController;

  // Weak reference to the internal binding state.
  std::weak_ptr<AsyncClientBinding> binding_;

  // The dispatcher that is monitoring FIDL messages.
  async_dispatcher_t* dispatcher_ = nullptr;

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

// |ClientController| manages the lifetime of a |ClientImpl| instance.
// The |ClientImpl| class needs to inherit from |fidl::internal::ClientBase|.
//
// |ClientImpl|s are created when binding a client endpoint to a message
// dispatcher, via |Bind|. The destruction of |ClientImpl|s is initiated when
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

  // Binds the client implementation to the |dispatcher| and |client_end|.
  // Takes ownership of |client_impl| and starts managing its lifetime.
  //
  // It is an error to call |Bind| more than once on the same controller.
  void Bind(std::shared_ptr<ClientBase>&& client_impl, zx::channel client_end,
            async_dispatcher_t* dispatcher, AsyncEventHandler* event_handler,
            fidl::AnyTeardownObserver&& teardown_observer, ThreadingPolicy threading_policy);

  // Begins to unbind the channel from the dispatcher. In particular, it
  // triggers the asynchronous destruction of the bound |ClientImpl|. May be
  // called from any thread. If provided, the |AsyncEventHandler::Unbound| is
  // invoked asynchronously on a dispatcher thread.
  //
  // |Bind| must have been called before this.
  void Unbind();

  bool is_valid() const { return static_cast<bool>(client_impl_); }
  explicit operator bool() const { return is_valid(); }

  ClientBase* get() const { return client_impl_.get(); }

 private:
  // |ControlBlock| controls the lifecycle of a client binding, such that
  // teardown will only happen after all clones of a |Client| managing
  // the same channel goes out of scope.
  //
  // Specifically, all clones of a |Client| will share the same |ControlBlock|
  // instance, which in turn references the |ClientImpl|, and is responsible
  // for its teardown via RAII.
  class ControlBlock final {
   public:
    explicit ControlBlock(std::shared_ptr<ClientBase> client) : client_impl_(std::move(client)) {}

    // Triggers teardown, which will cause any strong references to the
    // |ClientBase| to be released.
    ~ControlBlock() {
      if (client_impl_) {
        client_impl_->AsyncTeardown();
      }
    }

   private:
    std::shared_ptr<ClientBase> client_impl_;
  };

  std::shared_ptr<ClientBase> client_impl_;
  std::shared_ptr<ControlBlock> control_;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_CLIENT_BASE_H_
