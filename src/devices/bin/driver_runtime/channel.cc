// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_runtime/channel.h"

#include <lib/fdf/channel.h>

#include <fbl/auto_lock.h>

#include "src/devices/bin/driver_runtime/arena.h"
#include "src/devices/bin/driver_runtime/dispatcher.h"
#include "src/devices/bin/driver_runtime/driver_context.h"
#include "src/devices/bin/driver_runtime/handle.h"

namespace {

fdf_status_t CheckReadArgs(uint32_t options, fdf_arena_t** out_arena, void** out_data,
                           uint32_t* out_num_bytes, zx_handle_t** out_handles,
                           uint32_t* out_num_handles) {
  // |out_arena| is required except for empty messages.
  if (!out_arena && (out_data || out_handles)) {
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

}  // namespace

namespace driver_runtime {

// static
fdf_status_t Channel::Create(uint32_t options, fdf_handle_t* out0, fdf_handle_t* out1) {
  auto shared_state0 = fbl::AdoptRef(new FdfChannelSharedState());
  auto shared_state1 = shared_state0;

  auto ch0 = fbl::AdoptRef(new Channel(shared_state0));
  if (!ch0) {
    return ZX_ERR_NO_MEMORY;
  }
  auto ch1 = fbl::AdoptRef(new Channel(shared_state1));
  if (!ch1) {
    return ZX_ERR_NO_MEMORY;
  }

  ch0->Init(ch1);
  ch1->Init(ch0);

  auto handle0 = driver_runtime::Handle::Create(std::move(ch0));
  auto handle1 = driver_runtime::Handle::Create(std::move(ch1));
  if (!handle0 || !handle1) {
    return ZX_ERR_NO_RESOURCES;
  }

  *out0 = handle0->handle_value();
  *out1 = handle1->handle_value();

  // These handles will be reclaimed when they are closed.
  handle0.release();
  handle1.release();

  return ZX_OK;
}

// This is only called during |Create|, before the channels are returned,
// so we do not need locking.
void Channel::Init(const fbl::RefPtr<Channel>& peer) __TA_NO_THREAD_SAFETY_ANALYSIS {
  peer_ = peer;
}

fdf_status_t Channel::CheckWriteArgs(uint32_t options, fdf_arena_t* arena, void* data,
                                     uint32_t num_bytes, zx_handle_t* handles,
                                     uint32_t num_handles) {
  // Require an arena if data or handles are populated (empty messages are allowed).
  if (!arena && (data || handles)) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (data && !fdf_arena_contains(arena, data, num_bytes)) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (handles && !fdf_arena_contains(arena, handles, num_handles * sizeof(fdf_handle_t))) {
    return ZX_ERR_INVALID_ARGS;
  }
  for (uint32_t i = 0; i < num_handles; i++) {
    if (driver_runtime::Handle::IsFdfHandle(handles[i])) {
      fbl::RefPtr<Channel> transfer_channel;
      zx_status_t status = Handle::GetObject(handles[i], &transfer_channel);
      if (status != ZX_OK) {
        return ZX_ERR_INVALID_ARGS;
      }
      if (transfer_channel.get() == this) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      // TODO(fxbug.dev/87278): we should change ownership of the handle to disallow
      // the user calling wait right after we check it.
      if (transfer_channel->HasIncompleteWaitAsync()) {
        return ZX_ERR_INVALID_ARGS;
      }
    }
  }
  return ZX_OK;
}

fdf_status_t Channel::Write(uint32_t options, fdf_arena_t* arena, void* data, uint32_t num_bytes,
                            zx_handle_t* handles, uint32_t num_handles) {
  fdf_status_t status = CheckWriteArgs(options, arena, data, num_bytes, handles, num_handles);
  if (status != ZX_OK) {
    return status;
  }
  std::unique_ptr<CallbackRequest> callback_request;
  {
    fbl::AutoLock lock(get_lock());
    if (!peer_) {
      return ZX_ERR_PEER_CLOSED;
    }
    fbl::RefPtr<fdf_arena> arena_ref(arena);
    auto msg = MessagePacket::Create(std::move(arena_ref), data, num_bytes, handles, num_handles);
    if (!msg) {
      return ZX_ERR_NO_MEMORY;
    }
    callback_request = peer_->WriteSelfLocked(std::move(msg));
  }
  // Queue the callback outside of the lock.
  if (callback_request) {
    CallbackRequest::QueueOntoDispatcher(std::move(callback_request));
  }
  return ZX_OK;
}

std::unique_ptr<CallbackRequest> Channel::WriteSelfLocked(MessagePacketOwner msg) {
  if (!waiters_.is_empty()) {
    // If the far side is waiting for replies to messages sent via "call",
    // see if this message has a matching txid to one of the waiters, and if so, deliver it.
    fdf_txid_t txid = msg->get_txid();
    for (auto& waiter : waiters_) {
      // Deliver message to waiter.
      // Remove waiter from list.
      auto waiter_txid = waiter.get_txid();
      ZX_ASSERT(waiter_txid.has_value());
      if (waiter_txid.value() == txid) {
        waiters_.erase(waiter);
        waiter.DeliverLocked(std::move(msg));
        return nullptr;
      }
    }
  }
  msg_queue_.push_back(std::move(msg));
  // No dispatcher has been registered yet to handle callback requests.
  if (!dispatcher_) {
    return nullptr;
  }

  // If no read wait_async has been registered, we will not queue
  // the callback request yet.
  if (!IsCallbackRequestQueuedLocked() && IsWaitAsyncRegisteredLocked()) {
    return TakeCallbackRequestLocked(ZX_OK);
  }
  return nullptr;
}

fdf_status_t Channel::Read(uint32_t options, fdf_arena_t** out_arena, void** out_data,
                           uint32_t* out_num_bytes, zx_handle_t** out_handles,
                           uint32_t* out_num_handles) {
  fdf_status_t status =
      CheckReadArgs(options, out_arena, out_data, out_num_bytes, out_handles, out_num_handles);
  if (status != ZX_OK) {
    return status;
  }

  // Make sure this destructs outside of the lock.
  MessagePacketOwner msg;

  {
    fbl::AutoLock lock(get_lock());

    if (!peer_ && msg_queue_.is_empty()) {
      return ZX_ERR_PEER_CLOSED;
    }
    if (msg_queue_.is_empty()) {
      return ZX_ERR_SHOULD_WAIT;
    }
    msg = msg_queue_.pop_front();
    if (msg == nullptr) {
      return ZX_ERR_SHOULD_WAIT;
    }
    msg->CopyOut(out_arena, out_data, out_num_bytes, out_handles, out_num_handles);
  }
  return ZX_OK;
}

fdf_status_t Channel::WaitAsync(struct fdf_dispatcher* dispatcher, fdf_channel_read_t* channel_read,
                                uint32_t options) {
  std::unique_ptr<driver_runtime::CallbackRequest> callback_request;
  {
    fbl::AutoLock lock(get_lock());

    // If there are pending messages, we allow reading them even if the peer has already closed.
    if (!peer_ && msg_queue_.is_empty()) {
      return ZX_ERR_PEER_CLOSED;
    }

    // There is already a pending wait async.
    if (dispatcher_) {
      return ZX_ERR_BAD_STATE;
    }
    dispatcher_ = dispatcher;
    channel_read_ = channel_read;

    // We only queue one callback request at a time.
    ZX_ASSERT(!IsCallbackRequestQueuedLocked());

    // There might be no messages available yet, in which case we won't queue the request yet.
    if (!msg_queue_.is_empty()) {
      callback_request = TakeCallbackRequestLocked(ZX_OK);
    }
  }
  if (callback_request) {
    CallbackRequest::QueueOntoDispatcher(std::move(callback_request));
  }
  return ZX_OK;
}

fdf_txid_t Channel::AllocateTxidLocked() {
  fdf_txid_t txid;
  do {
    txid = kMinTxid + next_id_;
    ZX_ASSERT(txid >= kMinTxid);
    // Bitwise AND with |kNumTxids - 1| to handle wrap-around.
    next_id_ = (next_id_ + 1) & (kNumTxids - 1);

    // If there are waiting messages, ensure we have not allocated a txid
    // that's already in use. This is unlikely. It's atypical for multiple
    // threads to be invoking channel_call() on the same channel at once, so
    // the waiter list is most commonly empty.
  } while (IsTxidInUseLocked(txid));
  return txid;
}

bool Channel::IsTxidInUseLocked(fdf_txid_t txid) {
  for (Channel::MessageWaiter& w : waiters_) {
    auto waiter_txid = w.get_txid();
    ZX_ASSERT(waiter_txid.has_value());
    if (waiter_txid.value() == txid) {
      return true;
    }
  }
  return false;
}

fdf_status_t Channel::Call(uint32_t options, zx_time_t deadline,
                           const fdf_channel_call_args_t* args) {
  if (!args) {
    return ZX_ERR_INVALID_ARGS;
  }
  fdf_status_t status = CheckWriteArgs(options, args->wr_arena, args->wr_data, args->wr_num_bytes,
                                       args->wr_handles, args->wr_num_handles);
  if (status != ZX_OK) {
    return status;
  }
  status = CheckReadArgs(options, args->rd_arena, args->rd_data, args->rd_num_bytes,
                         args->rd_handles, args->rd_num_handles);
  if (status != ZX_OK) {
    return status;
  }
  if (args->wr_num_bytes < sizeof(fdf_txid_t)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Check if the thread is allowing synchronous calls.
  auto dispatcher = driver_context::GetCurrentDispatcher();
  if (dispatcher && !dispatcher->allow_sync_calls()) {
    return ZX_ERR_BAD_STATE;
  }

  fbl::RefPtr<fdf_arena> arena_ref(args->wr_arena);
  auto msg = MessagePacket::Create(std::move(arena_ref), args->wr_data, args->wr_num_bytes,
                                   args->wr_handles, args->wr_num_handles);
  if (!msg) {
    return ZX_ERR_NO_MEMORY;
  }

  MessageWaiter waiter(fbl::RefPtr(this));
  std::unique_ptr<CallbackRequest> callback_request;
  {
    fbl::AutoLock lock(get_lock());
    if (!peer_) {
      // Make sure the channel is cleared from the waiter before it destructs.
      auto reply = waiter.TakeLocked();
      ZX_ASSERT(!reply.is_ok());  // No reply is expected.
      return ZX_ERR_PEER_CLOSED;
    }

    fdf_txid_t txid = AllocateTxidLocked();
    // Install our txid in the waiter and the outbound message.
    waiter.set_txid(txid);
    msg->set_txid(txid);

    // Before writing the outbound message and waiting, add our waiter to the list.
    waiters_.push_back(&waiter);

    // Write outbound message to opposing endpoint.
    callback_request = peer_->WriteSelfLocked(std::move(msg));
  }

  // Queue any callback outside of the lock.
  if (callback_request) {
    CallbackRequest::QueueOntoDispatcher(std::move(callback_request));
  }

  // Wait until a message with the same txid is received, or the deadline is reached.
  waiter.Wait(deadline);
  // Rather than using the status of the wait, we should check the status recorded by the waiter.
  // This is as |Wait| is not called under lock, and the waiter could have been updated
  // (and removed from the |waiters_| list) while we were trying to acquire the waiter's lock.

  {
    fbl::AutoLock lock(get_lock());

    auto reply = waiter.TakeLocked();
    // If the wait timed out, the waiter would not have been removed from the list.
    // In the case of ZX_OK, or other error statuses (such as ZX_ERR_PEER_CLOSED),
    // the waiter would already be removed.
    if (reply.status_value() == ZX_ERR_TIMED_OUT) {
      waiters_.erase(waiter);
    }
    if (reply.is_ok()) {
      reply->CopyOut(args->rd_arena, args->rd_data, args->rd_num_bytes, args->rd_handles,
                     args->rd_num_handles);
    }
    return reply.status_value();
  }
}

void Channel::CancelWait() {
  std::unique_ptr<driver_runtime::CallbackRequest> callback_request;
  {
    fbl::AutoLock lock(get_lock());

    // Check if the client has registered a callback via |WaitAsync|.
    if (!IsWaitAsyncRegisteredLocked()) {
      return;
    }
    if (dispatcher_->unsynchronized()) {
      // If the callback has already been scheduled, we don't need to do anything.
      if (IsCallbackRequestQueuedLocked()) {
        return;
      }
      // If there were no pending messages we would not yet have queued it to the dispatcher.
      callback_request = TakeCallbackRequestLocked(ZX_ERR_CANCELED);

    } else {
      // For synchronized dispatchers, we always cancel the request synchronously.
      // Since we require |CancelWait| to be called on the dispatcher thread,
      // a callback request could be queued on the dispatcher, but not yet run.
      if (IsCallbackRequestQueuedLocked()) {
        ZX_ASSERT(unowned_callback_request_);
        callback_request = dispatcher_->CancelCallback(*unowned_callback_request_);
        // Cancellation should always be successful for synchronized dispatchers.
        ZX_ASSERT(callback_request);
        callback_request->Reset();
        callback_request_ = std::move(callback_request);
      }
      dispatcher_ = nullptr;
      channel_read_ = nullptr;
    }
  }
  if (callback_request) {
    CallbackRequest::QueueOntoDispatcher(std::move(callback_request));
  }
}

// We disable lock analysis here as it doesn't realize the lock is shared
// when trying to access the peer's internals.
// Make sure to acquire the lock before accessing class members or calling
// any _Locked class methods.
void Channel::Close() __TA_NO_THREAD_SAFETY_ANALYSIS {
  fbl::RefPtr<Channel> peer;
  {
    fbl::AutoLock lock(get_lock());

    peer = std::move(peer_);
    if (peer) {
      peer->peer_.reset();
    }
    // Abort any waiting Call operations because we've been canceled by reason
    // of the opposing endpoint going away.
    // Remove waiter from list.
    while (!waiters_.is_empty()) {
      auto waiter = waiters_.pop_front();
      waiter->CancelLocked(ZX_ERR_PEER_CLOSED);
    }
  }
  CancelWait();
  if (peer) {
    peer->OnPeerClosed();
  }
}

void Channel::OnPeerClosed() {
  std::unique_ptr<driver_runtime::CallbackRequest> callback_request;
  {
    fbl::AutoLock lock(get_lock());
    // Abort any waiting Call operations because we've been canceled by reason
    // of the opposing endpoint going away.
    // Remove waiter from list.
    while (!waiters_.is_empty()) {
      auto waiter = waiters_.pop_front();
      waiter->CancelLocked(ZX_ERR_PEER_CLOSED);
    }

    // If there are no messages queued, but we are waiting for a callback,
    // we should send the peer closed message now.
    if (msg_queue_.is_empty() && !IsCallbackRequestQueuedLocked() &&
        IsWaitAsyncRegisteredLocked()) {
      callback_request = TakeCallbackRequestLocked(ZX_ERR_PEER_CLOSED);
    }
  }
  if (callback_request) {
    CallbackRequest::QueueOntoDispatcher(std::move(callback_request));
  }
}

std::unique_ptr<driver_runtime::CallbackRequest> Channel::TakeCallbackRequestLocked(
    fdf_status_t callback_reason) {
  ZX_ASSERT(callback_request_);

  ZX_ASSERT(!callback_request_->IsPending());
  driver_runtime::Callback callback =
      [channel = fbl::RefPtr<Channel>(this)](
          std::unique_ptr<driver_runtime::CallbackRequest> callback_request, fdf_status_t status) {
        channel->DispatcherCallback(std::move(callback_request), status);
      };
  callback_request_->SetCallback(dispatcher_, std::move(callback), callback_reason);
  return std::move(callback_request_);
}

void Channel::DispatcherCallback(std::unique_ptr<driver_runtime::CallbackRequest> callback_request,
                                 fdf_status_t status) {
  ZX_ASSERT(!callback_request->IsPending());

  fdf_dispatcher_t* dispatcher = nullptr;
  fdf_channel_read_t* channel_read = nullptr;
  {
    fbl::AutoLock lock(get_lock());

    // We should only have queued the callback request if a read wait_async
    // had been registered.
    ZX_ASSERT(dispatcher_ && channel_read_);
    // Clear these fields before calling the callback, so that calling |WaitAsync|
    // won't return an error.
    std::swap(dispatcher, dispatcher_);
    std::swap(channel_read, channel_read_);

    // Take ownership of the callback request so we can reuse it later.
    callback_request_ = std::move(callback_request);
    num_pending_callbacks_++;
  }
  ZX_ASSERT(dispatcher);
  ZX_ASSERT(channel_read);
  ZX_ASSERT(channel_read->handler);

  channel_read->handler(dispatcher, channel_read, status);
  {
    fbl::AutoLock lock(get_lock());
    ZX_ASSERT(num_pending_callbacks_ > 0);
    num_pending_callbacks_--;
  }
}

Channel::MessageWaiter::~MessageWaiter() {
  ZX_ASSERT(!channel_);
  ZX_ASSERT(!InContainer());
}

void Channel::MessageWaiter::DeliverLocked(MessagePacketOwner msg) {
  ZX_ASSERT(channel_);

  msg_ = std::move(msg);
  status_ = ZX_OK;
  completion_.Signal();
}

void Channel::MessageWaiter::CancelLocked(zx_status_t status) {
  ZX_ASSERT(!InContainer());
  ZX_ASSERT(channel_);
  status_ = status;
  completion_.Signal();
}

void Channel::MessageWaiter::Wait(zx_time_t deadline) {
  ZX_ASSERT(channel_);

  zx_duration_t duration = zx_time_sub_time(deadline, zx_clock_get_monotonic());
  // We do not use the status of the wait. Either the channel updates the status
  // once it delivers the message or cancels the wait, or ZX_ERR_TIMED_OUT is assumed.
  __UNUSED zx_status_t status = completion_.Wait(zx::duration(duration));
}

zx::status<MessagePacketOwner> Channel::MessageWaiter::TakeLocked() {
  ZX_ASSERT(channel_);

  channel_ = nullptr;
  if (!status_.has_value()) {
    // We did not receive any response for the channel call.
    return zx::error(ZX_ERR_TIMED_OUT);
  }
  if (status_.value() != ZX_OK) {
    return zx::error(status_.value());
  }
  return zx::ok(std::move(msg_));
}

}  // namespace driver_runtime
