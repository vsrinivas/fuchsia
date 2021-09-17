// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/dispatcher.h>
#include <lib/async/task.h>
#include <lib/async/time.h>
#include <lib/fidl/epitaph.h>
#include <lib/fidl/llcpp/async_binding.h>
#include <lib/fidl/llcpp/async_transaction.h>
#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl/trace.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include <type_traits>

namespace fidl {
namespace internal {

AsyncBinding::AsyncBinding(async_dispatcher_t* dispatcher, const zx::unowned_channel& channel,
                           ThreadingPolicy threading_policy)
    : async_wait_t({{ASYNC_STATE_INIT},
                    &AsyncBinding::OnMessage,
                    channel->get(),
                    ZX_CHANNEL_PEER_CLOSED | ZX_CHANNEL_READABLE,
                    0}),
      dispatcher_(dispatcher),
      thread_checker_(threading_policy) {
  ZX_ASSERT(dispatcher_);
  ZX_ASSERT(handle() != ZX_HANDLE_INVALID);
}

void AsyncBinding::MessageHandler(zx_status_t dispatcher_status, const zx_packet_signal_t* signal) {
  ScopedThreadGuard guard(thread_checker_);
  ZX_ASSERT(keep_alive_);

  if (dispatcher_status != ZX_OK)
    return PerformTeardown(UnbindInfo::DispatcherError(dispatcher_status));

  if (signal->observed & ZX_CHANNEL_READABLE) {
    FIDL_INTERNAL_DISABLE_AUTO_VAR_INIT InlineMessageBuffer<ZX_CHANNEL_MAX_MSG_BYTES> bytes;
    FIDL_INTERNAL_DISABLE_AUTO_VAR_INIT zx_handle_info_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    for (uint64_t i = 0; i < signal->count; i++) {
      fidl_trace(WillLLCPPAsyncChannelRead);
      IncomingMessage msg = fidl::ChannelReadEtc(handle(), 0, bytes.view(), cpp20::span(handles));
      if (!msg.ok()) {
        return PerformTeardown(fidl::UnbindInfo{msg});
      }
      fidl_trace(DidLLCPPAsyncChannelRead, nullptr /* type */, bytes.data(), msg.byte_actual(),
                 msg.handle_actual());

      // Flag indicating whether this thread still has access to the binding.
      bool binding_released = false;
      // Dispatch the message.
      cpp17::optional<fidl::UnbindInfo> maybe_error = Dispatch(msg, &binding_released);

      // If binding_released is not set, keep_alive_ is still valid and this thread will continue to
      // read messages on this binding.
      if (binding_released)
        return;
      ZX_ASSERT(keep_alive_);

      // If there was any error enabling dispatch or an unexpected message, destroy the binding.
      if (maybe_error)
        return PerformTeardown(*maybe_error);
    }

    if (CheckForTeardownAndBeginNextWait() != ZX_OK)
      return PerformTeardown(cpp17::nullopt);
  } else {
    ZX_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
    return PerformTeardown(UnbindInfo::PeerClosed(ZX_ERR_PEER_CLOSED));
  }
}

void AsyncBinding::BeginFirstWait() {
  zx_status_t status;
  {
    std::scoped_lock lock(lock_);
    ZX_ASSERT(lifecycle_.Is(Lifecycle::kCreated));
    status = async_begin_wait(dispatcher_, this);
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
      zx_status_t status = async_begin_wait(dispatcher_, this);
      if (status != ZX_OK)
        lifecycle_.TransitionToMustTeardown(fidl::UnbindInfo::DispatcherError(status));
      return status;
    }

    default:
      // Other lifecycle states are illegal.
      __builtin_abort();
  }
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

  // A boolean value that will become available in the future. |Get| will block
  // until |Set| is invoked once with the value.
  class FutureBool {
   public:
    void Set(bool value) {
      value_ = value;
      sync_completion_signal(&result_ready_);
    }

    bool Get() {
      zx_status_t status = sync_completion_wait(&result_ready_, ZX_TIME_INFINITE);
      ZX_DEBUG_ASSERT(status == ZX_OK);
      return value_;
    }

   private:
    bool value_ = false;
    sync_completion_t result_ready_ = {};
  };
  std::shared_ptr message_handler_pending = std::make_shared<FutureBool>();

  // Attempt to add a task to teardown the bindings. On failure, the dispatcher
  // was shutdown; the message handler would notice and perform the teardown.
  class TeardownTask : private async_task_t {
   public:
    static zx_status_t Post(async_dispatcher_t* dispatcher,
                            std::weak_ptr<AsyncBinding> weak_binding,
                            std::shared_ptr<FutureBool> message_handler_pending) {
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

      if (self->message_handler_pending_->Get())
        return;
      self->message_handler_pending_.reset();

      // If |weak_binding_| fails to lock to a strong reference, that means the
      // binding was already torn down by the message handler. This will never
      // happen because we return early if a message handler is pending.
      auto binding = self->weak_binding_.lock();
      ZX_DEBUG_ASSERT(binding);
      auto* binding_raw = binding.get();
      // |binding->keep_alive_| is at least another reference.
      ZX_DEBUG_ASSERT(!binding.unique());
      binding.reset();

      ScopedThreadGuard guard(binding_raw->thread_checker_);
      // At this point, no other thread will touch the internal reference.
      // Either the message handler never started or was canceled.
      binding_raw->PerformTeardown(cpp17::nullopt);
    }

   private:
    TeardownTask(async_dispatcher_t* dispatcher, std::weak_ptr<AsyncBinding> weak_binding,
                 std::shared_ptr<FutureBool> message_handler_pending)
        : async_task_t({{ASYNC_STATE_INIT}, &TeardownTask::Invoke, async_now(dispatcher)}),
          weak_binding_(std::move(weak_binding)),
          message_handler_pending_(std::move(message_handler_pending)) {}

    std::weak_ptr<AsyncBinding> weak_binding_;
    std::shared_ptr<FutureBool> message_handler_pending_;
  };

  // We need to first post the teardown task, then attempt to cancel the message
  // handler, and block the teardown task until the cancellation result is ready
  // using a |FutureBool|. This is because the dispatcher might be shut down in
  // between the posting and the cancelling. If we tried to cancel first then
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
      // handler is driving/will drive the teardown process.
      zx_status_t status = async_cancel_wait(dispatcher_, this);
      ZX_DEBUG_ASSERT(status == ZX_OK || status == ZX_ERR_NOT_FOUND);
      message_handler_pending->Set(status != ZX_OK);
    } else {
      message_handler_pending->Set(false);
    }
  }

  return TeardownTaskPostingResult::kOk;
}

void AsyncBinding::PerformTeardown(cpp17::optional<UnbindInfo> info) {
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

std::optional<UnbindInfo> AnyAsyncServerBinding::Dispatch(fidl::IncomingMessage& msg,
                                                          bool* binding_released) {
  auto* hdr = msg.header();
  AsyncTransaction txn(hdr->txid, binding_released);
  return txn.Dispatch(std::move(keep_alive_), std::move(msg));
}

//
// Client binding specifics
//

std::shared_ptr<AsyncClientBinding> AsyncClientBinding::Create(
    async_dispatcher_t* dispatcher, std::shared_ptr<zx::channel> channel,
    std::shared_ptr<ClientBase> client, AsyncEventHandler* event_handler,
    AnyTeardownObserver&& teardown_observer, ThreadingPolicy threading_policy) {
  auto ret = std::shared_ptr<AsyncClientBinding>(
      new AsyncClientBinding(dispatcher, std::move(channel), std::move(client), event_handler,
                             std::move(teardown_observer), threading_policy));
  ret->keep_alive_ = ret;  // Keep the binding alive until teardown.
  return ret;
}

AsyncClientBinding::AsyncClientBinding(async_dispatcher_t* dispatcher,
                                       std::shared_ptr<zx::channel> channel,
                                       std::shared_ptr<ClientBase> client,
                                       AsyncEventHandler* event_handler,
                                       AnyTeardownObserver&& teardown_observer,
                                       ThreadingPolicy threading_policy)
    : AsyncBinding(dispatcher, zx::unowned_channel(channel->get()), threading_policy),
      channel_(std::move(channel)),
      client_(std::move(client)),
      event_handler_(event_handler),
      teardown_observer_(std::move(teardown_observer)) {}

std::optional<UnbindInfo> AsyncClientBinding::Dispatch(fidl::IncomingMessage& msg, bool*) {
  return client_->Dispatch(msg, event_handler_);
}

void AsyncClientBinding::FinishTeardown(std::shared_ptr<AsyncBinding>&& calling_ref,
                                        UnbindInfo info) {
  // Move binding into scope.
  std::shared_ptr<AsyncBinding> binding = std::move(calling_ref);

  // Stash state required after deleting the binding.
  AnyTeardownObserver teardown_observer = std::move(teardown_observer_);
  AsyncEventHandler* event_handler = event_handler_;
  std::shared_ptr<ClientBase> client = std::move(client_);

  // Delete the calling reference.
  // We are not returning the channel to the user, so don't wait for transient
  // references to go away.
  binding = nullptr;

  // There could be residual references to the binding, but those are only held
  // briefly when obtaining the channel. To be conservative, assume that `this`
  // is no longer valid past this point.

  // Outstanding async responses will no longer be received, so release the contexts.
  client->ReleaseResponseContexts(info);
  client = nullptr;

  // Execute the error hook if specified.
  if (info.reason() != fidl::Reason::kUnbind) {
    if (event_handler != nullptr)
      event_handler->on_fidl_error(info);
  }

  // Notify teardown.
  std::move(teardown_observer).Notify();
}

}  // namespace internal
}  // namespace fidl
