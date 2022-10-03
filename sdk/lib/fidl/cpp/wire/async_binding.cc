// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/dispatcher.h>
#include <lib/async/task.h>
#include <lib/async/time.h>
#include <lib/fidl/cpp/wire/async_binding.h>
#include <lib/fidl/cpp/wire/async_transaction.h>
#include <lib/fidl/cpp/wire/client_base.h>
#include <lib/fidl/cpp/wire/internal/transport.h>
#include <lib/fidl/epitaph.h>
#include <lib/fidl/trace.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include <type_traits>

namespace fidl {
namespace internal {

bool DispatchError::RequiresImmediateTeardown() {
  // Do not immediately teardown the bindings if some FIDL method failed to
  // write to the transport due to peer closed. The message handler in
  // |AsyncBinding| will eventually discover that the transport is in the peer
  // closed state and begin teardown, so we are not ignoring this error just
  // deferring it.
  //
  // To see why this is necessary, consider a FIDL method that is supposed to
  // shutdown the server connection. Upon processing this FIDL method, the
  // server may send a reply or a terminal event, and then close their endpoint.
  // The server might have also sent other replies or events that are waiting to
  // be read by the client. If the client immediately unbinds on the first call
  // hitting peer closed, we would be dropping any unread messages that the
  // server have sent. In other words, whether the terminal events etc. are
  // surfaced to the user or discarded would depend on whether the user just
  // happened to make another call after the server closed their endpoint, which
  // is an undesirable race condition. By deferring the handling of peer closed
  // errors, we ensure that any messages the server sent prior to closing the
  // endpoint will be reliably drained by the client and exposed to the user. An
  // equivalent situation applies in the server bindings in ensuring that client
  // messages are reliably drained after peer closed.
  return !(origin == fidl::ErrorOrigin::kSend && info.reason() == fidl::Reason::kPeerClosed);
}

AsyncBinding::AsyncBinding(async_dispatcher_t* dispatcher, AnyUnownedTransport transport,
                           ThreadingPolicy threading_policy)
    : dispatcher_(dispatcher),
      transport_(transport),
      thread_checker_(dispatcher, threading_policy) {
  ZX_ASSERT(dispatcher_);
  ZX_ASSERT(transport_.is_valid());
  transport_.create_waiter(
      dispatcher,
      [this](fidl::IncomingHeaderAndMessage& msg, internal::MessageStorageViewBase* storage_view) {
        this->MessageHandler(msg, storage_view);
      },
      [this](UnbindInfo info) { this->WaitFailureHandler(info); }, any_transport_waiter_);
}

void AsyncBinding::MessageHandler(fidl::IncomingHeaderAndMessage& msg,
                                  internal::MessageStorageViewBase* storage_view) {
  ScopedThreadGuard guard(thread_checker_);
  ZX_ASSERT(keep_alive_);

  // Flag indicating whether this thread still has access to the binding.
  bool next_wait_begun_early = false;
  // Dispatch the message.
  std::optional<DispatchError> maybe_error = Dispatch(msg, &next_wait_begun_early, storage_view);
  // If |next_wait_begun_early| is true, then the interest for the next
  // message had been eagerly registered in the method handler, and another
  // thread may already be running |MessageHandler|. We should exit without
  // attempting to register yet another wait or attempting to modify the
  // binding state here.
  if (next_wait_begun_early)
    return;

  // If there was any error enabling dispatch or an unexpected message, destroy the binding.
  if (maybe_error) {
    if (maybe_error->RequiresImmediateTeardown()) {
      return PerformTeardown(maybe_error->info);
    }
  }

  if (CheckForTeardownAndBeginNextWait() != ZX_OK)
    return PerformTeardown(std::nullopt);
}

void AsyncBinding::WaitFailureHandler(UnbindInfo info) {
  // When a dispatcher error of |ZX_ERR_CANCELED| happens, it indicates that the
  // dispatcher is shutting down. We have ensured that the dispatcher is
  // single-threaded via thread checkers at various places during message
  // dispatch. Here, we can relax the thread checking since there will not be
  // any parallel up-calls to user objects during shutdown under a
  // single-threaded dispatcher.
  if (info.reason() == fidl::Reason::kDispatcherError && info.status() == ZX_ERR_CANCELED) {
    thread_checker_.assume_exclusive();
  } else {
    thread_checker_.check();
  }

  ZX_ASSERT(keep_alive_);
  return PerformTeardown(info);
}

void AsyncBinding::BeginFirstWait() {
  zx_status_t status;
  {
    std::scoped_lock lock(lock_);
    ZX_ASSERT(lifecycle_.Is(Lifecycle::kCreated));
    status = any_transport_waiter_->Begin();
    if (status == ZX_OK) {
      lifecycle_.TransitionToBound();
      return;
    }
  }

  // If the first |async_begin_wait| failed, attempt to report the error through
  // the |on_unbound| handler - the interface was effectively unbound
  // immediately on first dispatch.
  //
  // There are two possible error cases:
  //
  // - The server endpoint does not have the |ZX_RIGHT_WAIT| right. Since the
  //   server endpoint may be of foreign origin, asynchronously report the error
  //   through the |on_unbound| handler.
  //
  // - The dispatcher does not support waiting on a port, or was shutdown. This
  //   is a programming error. The user code should either switch to a
  //   supporting dispatcher, or properly implement teardown by not shutting
  //   down the event loop until all current incoming events have been
  //   processed.
  //
  using Result = AsyncBinding::TeardownTaskPostingResult;
  Result result =
      StartTeardownWithInfo(std::shared_ptr(keep_alive_), UnbindInfo::DispatcherError(status));
  switch (result) {
    case Result::kDispatcherError:
      // We are crashing the process anyways, but clearing |keep_alive_| helps
      // death-tests pass the leak-sanitizer.
      keep_alive_ = nullptr;
      ZX_PANIC(
          "When binding FIDL connection: "
          "dispatcher was shutdown, or unsupported dispatcher.");
    case Result::kRacedWithInProgressTeardown:
      // Should never happen - the binding was only just created.
      __builtin_unreachable();
    case Result::kOk:
      return;
  }
}

zx_status_t AsyncBinding::CheckForTeardownAndBeginNextWait() {
  std::scoped_lock lock(lock_);

  switch (lifecycle_.state()) {
    case Lifecycle::kMustTeardown:
      return ZX_ERR_CANCELED;

    case Lifecycle::kBound: {
      zx_status_t status = any_transport_waiter_->Begin();
      if (status != ZX_OK)
        lifecycle_.TransitionToMustTeardown(fidl::UnbindInfo::DispatcherError(status));
      return status;
    }

    default:
      // Other lifecycle states are illegal.
      __builtin_abort();
  }
}

void AsyncBinding::HandleError(std::shared_ptr<AsyncBinding>&& calling_ref, DispatchError error) {
  if (error.RequiresImmediateTeardown()) {
    StartTeardownWithInfo(std::move(calling_ref), error.info);
  }
}

bool AsyncBinding::IsDestructionImminent() const {
  std::scoped_lock lock(lock_);
  return lifecycle_.Is(Lifecycle::kMustTeardown) || lifecycle_.Is(Lifecycle::kTorndown);
}

auto AsyncBinding::StartTeardownWithInfo(std::shared_ptr<AsyncBinding>&& calling_ref,
                                         UnbindInfo info) -> TeardownTaskPostingResult {
  ScopedThreadGuard guard(thread_checker_);
  ZX_ASSERT(calling_ref);
  // Move the calling reference into this scope.
  auto binding = std::move(calling_ref);

  {
    std::scoped_lock lock(lock_);
    if (lifecycle_.Is(Lifecycle::kMustTeardown) || lifecycle_.Is(Lifecycle::kTorndown))
      return TeardownTaskPostingResult::kRacedWithInProgressTeardown;
    lifecycle_.TransitionToMustTeardown(info);
  }

  // A |CancellationResult| value that will become available in the future.
  // |Get| will block until |Set| is invoked once with the value.
  class FutureResult {
   public:
    using Result = TransportWaiter::CancellationResult;

    void Set(Result value) {
      value_ = value;
      sync_completion_signal(&result_ready_);
    }

    Result Get() {
      zx_status_t status = sync_completion_wait(&result_ready_, ZX_TIME_INFINITE);
      ZX_DEBUG_ASSERT(status == ZX_OK);
      return value_;
    }

   private:
    Result value_ = Result::kOk;
    sync_completion_t result_ready_ = {};
  };
  std::shared_ptr message_handler_pending = std::make_shared<FutureResult>();

  // Attempt to add a task to teardown the bindings. On failure, the dispatcher
  // was shutdown; the message handler would notice and perform the teardown.
  class TeardownTask : private async_task_t {
   public:
    static zx_status_t Post(async_dispatcher_t* dispatcher,
                            std::weak_ptr<AsyncBinding> weak_binding,
                            std::shared_ptr<FutureResult> message_handler_pending) {
      auto* task = new TeardownTask{
          dispatcher,
          std::move(weak_binding),
          std::move(message_handler_pending),
      };
      zx_status_t status = async_post_task(dispatcher, task);
      if (status != ZX_OK)
        delete task;
      return status;
    }

    static void Invoke(async_dispatcher_t* /*unused*/, async_task_t* task, zx_status_t status) {
      auto* self = static_cast<TeardownTask*>(task);
      struct Deferred {
        TeardownTask* task;
        ~Deferred() { delete task; }
      } deferred{self};

      TransportWaiter::CancellationResult result = self->message_handler_pending_->Get();
      self->message_handler_pending_.reset();

      if (result == TransportWaiter::CancellationResult::kDispatcherContextNeeded) {
        // Try teardown again from a dispatcher task.
        auto binding = self->weak_binding_.lock();
        if (!binding) {
          // If |weak_binding_| fails to lock to a strong reference, that means
          // the binding was already torn down by the message handler. This may
          // happen if the dispatcher was shutdown, waking the message handler
          // and tearing down the binding.
          return;
        }
        result = binding->any_transport_waiter_->Cancel();
      }

      switch (result) {
        case TransportWaiter::CancellationResult::kOk:
          break;
        case TransportWaiter::CancellationResult::kNotFound:
          // The message handler is driving/will drive the teardown process.
          return;
        case TransportWaiter::CancellationResult::kDispatcherContextNeeded:
          ZX_PANIC("Already in dispatcher context");
        case TransportWaiter::CancellationResult::kNotSupported:
          ZX_PANIC("Dispatcher must support canceling waits");
      }

      // If |weak_binding_| fails to lock to a strong reference, that means the
      // binding was already torn down by the message handler. This will never
      // happen because we return early if a message handler is pending.
      auto binding = self->weak_binding_.lock();
      ZX_DEBUG_ASSERT(binding);
      auto* binding_raw = binding.get();
      // |binding->keep_alive_| is at least another reference.
      ZX_DEBUG_ASSERT(!binding.unique());
      binding.reset();

      // At this point, no other thread will touch the internal reference.
      // Either the message handler never started or was canceled.
      // Therefore, we can relax any thread checking here, since there are no
      // parallel up-calls to user objects regardless of the current thread.
      binding_raw->thread_checker_.assume_exclusive();
      binding_raw->PerformTeardown(std::nullopt);
    }

   private:
    TeardownTask(async_dispatcher_t* dispatcher, std::weak_ptr<AsyncBinding> weak_binding,
                 std::shared_ptr<FutureResult> message_handler_pending)
        : async_task_t({{ASYNC_STATE_INIT}, &TeardownTask::Invoke, async_now(dispatcher)}),
          weak_binding_(std::move(weak_binding)),
          message_handler_pending_(std::move(message_handler_pending)) {}

    std::weak_ptr<AsyncBinding> weak_binding_;
    std::shared_ptr<FutureResult> message_handler_pending_;
  };

  // We need to first post the teardown task, then attempt to cancel the message
  // handler, and block the teardown task until the cancellation result is ready
  // using a |FutureResult|. This is because the dispatcher might be shut down
  // in between the posting and the cancelling. If we tried to cancel first then
  // post a task, we might end up in a difficult situation where the message
  // handler was successfully canceled, but the dispatcher was also shut down,
  // preventing us from posting any more tasks. Then we would run out of threads
  // from which to notify the user of teardown completion.
  //
  // This convoluted dance could be improved if |async_dispatcher_t| supported
  // interrupting a wait with an error passed to the handler, as opposed to
  // silent cancellation.
  if (TeardownTask::Post(dispatcher_, binding, message_handler_pending) != ZX_OK)
    return TeardownTaskPostingResult::kDispatcherError;

  {
    std::scoped_lock lock(lock_);
    if (lifecycle_.DidBecomeBound()) {
      // Attempt to cancel the current message handler. On failure, the message
      // handler or the teardown task will be responsible for driving the
      // teardown process.
      message_handler_pending->Set(any_transport_waiter_->Cancel());
    } else {
      message_handler_pending->Set(TransportWaiter::CancellationResult::kOk);
    }
  }

  return TeardownTaskPostingResult::kOk;
}

void AsyncBinding::PerformTeardown(std::optional<UnbindInfo> info) {
  auto binding = std::move(keep_alive_);

  fidl::UnbindInfo stored_info;
  {
    std::scoped_lock lock(lock_);
    if (info.has_value())
      lifecycle_.TransitionToMustTeardown(info.value());
    stored_info = lifecycle_.TransitionToTorndown();
  }

  FinishTeardown(std::move(binding), stored_info);
}

void AsyncBinding::Lifecycle::TransitionToBound() {
  ZX_DEBUG_ASSERT(Is(kCreated));
  state_ = kBound;
  did_enter_bound_ = true;
}

void AsyncBinding::Lifecycle::TransitionToMustTeardown(fidl::UnbindInfo info) {
  ZX_DEBUG_ASSERT(Is(kCreated) || Is(kBound) || Is(kMustTeardown));
  if (!Is(kMustTeardown)) {
    state_ = kMustTeardown;
    info_ = info;
  }
}

fidl::UnbindInfo AsyncBinding::Lifecycle::TransitionToTorndown() {
  ZX_DEBUG_ASSERT(Is(kMustTeardown));
  fidl::UnbindInfo info = info_;
  state_ = kTorndown;
  info_ = {};
  return info;
}

//
// Server binding specifics
//

std::shared_ptr<AsyncServerBinding> AsyncServerBinding::Create(
    async_dispatcher_t* dispatcher, fidl::internal::AnyTransport&& server_end,
    IncomingMessageDispatcher* interface, AnyOnUnboundFn&& on_unbound_fn) {
  auto binding = std::make_shared<AsyncServerBinding>(dispatcher, std::move(server_end), interface,
                                                      std::move(on_unbound_fn), ConstructionKey{});
  binding->InitKeepAlive();
  return binding;
}

std::optional<DispatchError> AsyncServerBinding::Dispatch(
    fidl::IncomingHeaderAndMessage& msg, bool* next_wait_begun_early,
    internal::MessageStorageViewBase* storage_view) {
  auto* hdr = msg.header();
  SyncTransaction txn(hdr->txid, this, next_wait_begun_early);
  return txn.Dispatch(std::move(msg), storage_view);
}

void AsyncServerBinding::FinishTeardown(std::shared_ptr<AsyncBinding>&& calling_ref,
                                        UnbindInfo info) {
  // Stash required state after deleting the binding, since the binding
  // will be destroyed as part of this function.
  auto* the_interface = interface();
  auto on_unbound_fn = std::move(on_unbound_fn_);

  // Downcast to our class.
  std::shared_ptr<AsyncServerBinding> server_binding =
      std::static_pointer_cast<AsyncServerBinding>(calling_ref);
  calling_ref.reset();

  // Delete the calling reference.
  // Wait for any transient references to be released.
  DestroyAndExtract(
      std::move(server_binding), &AsyncServerBinding::server_end_,
      [&info, the_interface, &on_unbound_fn](fidl::internal::AnyTransport server_end) {
        // `this` is no longer valid.

        // If required, send the epitaph.
        if (info.reason() == Reason::kClose) {
          ZX_ASSERT(server_end.type() == FIDL_TRANSPORT_TYPE_CHANNEL);
          info = UnbindInfo::Close(fidl_epitaph_write(server_end.handle(), info.status()));
        }

        // Execute the unbound hook if specified.
        if (on_unbound_fn)
          on_unbound_fn(the_interface, info, std::move(server_end));
      });
}

//
// Client binding specifics
//

std::shared_ptr<AsyncClientBinding> AsyncClientBinding::Create(
    async_dispatcher_t* dispatcher, std::shared_ptr<fidl::internal::AnyTransport> transport,
    std::shared_ptr<ClientBase> client, AsyncEventHandler* error_handler,
    AnyTeardownObserver&& teardown_observer, ThreadingPolicy threading_policy) {
  auto binding = std::shared_ptr<AsyncClientBinding>(
      new AsyncClientBinding(dispatcher, std::move(transport), std::move(client), error_handler,
                             std::move(teardown_observer), threading_policy));
  binding->InitKeepAlive();
  return binding;
}

AsyncClientBinding::AsyncClientBinding(async_dispatcher_t* dispatcher,
                                       std::shared_ptr<fidl::internal::AnyTransport> transport,
                                       std::shared_ptr<ClientBase> client,
                                       AsyncEventHandler* error_handler,
                                       AnyTeardownObserver&& teardown_observer,
                                       ThreadingPolicy threading_policy)
    : AsyncBinding(dispatcher, transport->borrow(), threading_policy),
      transport_(std::move(transport)),
      client_(std::move(client)),
      error_handler_(error_handler),
      teardown_observer_(std::move(teardown_observer)) {}

std::optional<DispatchError> AsyncClientBinding::Dispatch(
    fidl::IncomingHeaderAndMessage& msg, bool*, internal::MessageStorageViewBase* storage_view) {
  std::optional<UnbindInfo> info = client_->Dispatch(msg, storage_view);
  if (info.has_value()) {
    // A client binding does not propagate synchronous sending errors as part of
    // handling a message. All client callbacks return `void`.
    return DispatchError{*info, fidl::ErrorOrigin::kReceive};
  }
  return std::nullopt;
}

void AsyncClientBinding::FinishTeardown(std::shared_ptr<AsyncBinding>&& calling_ref,
                                        UnbindInfo info) {
  // Move binding into scope.
  std::shared_ptr<AsyncBinding> binding = std::move(calling_ref);

  // Stash state required after deleting the binding.
  AnyTeardownObserver teardown_observer = std::move(teardown_observer_);
  AsyncEventHandler* error_handler = error_handler_;
  std::shared_ptr<ClientBase> client = std::move(client_);

  // Delete the calling reference.
  // We are not returning the transport to the user, so don't wait for transient
  // references to go away.
  binding = nullptr;

  // There could be residual references to the binding, but those are only held
  // briefly when obtaining the transport. To be conservative, assume that `this`
  // is no longer valid past this point.

  // Outstanding async responses will no longer be received, so release the contexts.
  client->ReleaseResponseContexts(info);
  client = nullptr;

  // Execute the error hook if specified.
  if (info.reason() != fidl::Reason::kUnbind) {
    if (error_handler != nullptr)
      error_handler->on_fidl_error(info);
  }

  // Notify teardown.
  std::move(teardown_observer).Notify();
}

}  // namespace internal
}  // namespace fidl
