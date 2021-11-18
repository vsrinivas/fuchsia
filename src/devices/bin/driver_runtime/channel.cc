// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_runtime/channel.h"

#include <lib/fdf/channel.h>

#include <fbl/auto_lock.h>

#include "src/devices/bin/driver_runtime/arena.h"
#include "src/devices/bin/driver_runtime/dispatcher.h"
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

// We disable lock analysis here as it doesn't realize the lock is shared
// when trying to access the peer's internals.
// Make sure to acquire the lock before accessing class members or calling
// any _Locked class methods.
void Channel::Close() __TA_NO_THREAD_SAFETY_ANALYSIS {
  fbl::RefPtr<Channel> peer;
  std::unique_ptr<driver_runtime::CallbackRequest> callback_request;
  bool cancel_callback = false;
  {
    fbl::AutoLock lock(get_lock());

    peer = std::move(peer_);
    if (peer) {
      peer->peer_.reset();
    }
    // The client may have registered a callback via |WaitAsync|.
    if (IsWaitAsyncRegisteredLocked()) {
      if (dispatcher_->unsynchronized()) {
        // Make sure the callback is scheduled.
        // If there were no pending messages we would not yet have queued it to the dispatcher.
        if (!IsCallbackRequestQueuedLocked()) {
          callback_request = TakeCallbackRequestLocked(ZX_ERR_CANCELED);
        }
      } else {
        // Cancel any pending callback.
        if (IsCallbackRequestQueuedLocked()) {
          cancel_callback = true;
        }
      }
    }
  }
  if (callback_request) {
    CallbackRequest::QueueOntoDispatcher(std::move(callback_request));
  }
  if (cancel_callback) {
    ZX_ASSERT(unowned_callback_request_);
    // Since we require |Close| to be called on the dispatcher thread,
    // a callback request could be queued on the dispatcher, but not yet run.
    dispatcher_->CancelCallback(*unowned_callback_request_);
  }
  if (peer) {
    peer->OnPeerClosed();
  }
}

void Channel::OnPeerClosed() {
  std::unique_ptr<driver_runtime::CallbackRequest> callback_request;
  {
    fbl::AutoLock lock(get_lock());
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

}  // namespace driver_runtime
