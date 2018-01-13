// Copyright 2013 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/internal/connector.h"

#include <async/default.h>
#include <zircon/assert.h>
#include <zx/time.h>

namespace fidl {
namespace internal {
namespace {

constexpr zx_signals_t kSignals = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;

}  // namespace

// ----------------------------------------------------------------------------

Connector::Connector(zx::channel channel)
    : channel_(std::move(channel)),
      wait_(async_get_default(), channel_.get(), kSignals,
            ASYNC_FLAG_HANDLE_SHUTDOWN),
      incoming_receiver_(nullptr),
      error_(false),
      drop_writes_(false),
      enforce_errors_from_incoming_receiver_(true),
      destroyed_flag_(nullptr) {
  wait_.set_handler(fbl::BindMember(this, &Connector::OnHandleReady));
  // Even though we don't have an incoming receiver, we still want to monitor
  // the channel to know if is closed or encounters an error.
  zx_status_t status = wait_.Begin();
  ZX_ASSERT(status == ZX_OK);
}

Connector::~Connector() {
  if (destroyed_flag_)
    *destroyed_flag_ = true;
}

void Connector::CloseChannel() {
  wait_.Cancel();
  wait_.set_object(ZX_HANDLE_INVALID);
  channel_.reset();
}

zx::channel Connector::PassChannel() {
  wait_.Cancel();
  wait_.set_object(ZX_HANDLE_INVALID);
  return std::move(channel_);
}

bool Connector::WaitForIncomingMessageUntil(zx::time deadline) {
  if (error_)
    return false;

  zx_signals_t pending = ZX_SIGNAL_NONE;
  zx_status_t rv = channel_.wait_one(kSignals, deadline, &pending);
  if (rv == ZX_ERR_SHOULD_WAIT || rv == ZX_ERR_TIMED_OUT)
    return false;
  if (rv != ZX_OK) {
    NotifyError();
    return false;
  }
  if (pending & ZX_CHANNEL_READABLE) {
    ReadSingleMessage(&rv);
    return rv == ZX_OK;
  }

  ZX_DEBUG_ASSERT(pending & ZX_CHANNEL_PEER_CLOSED);
  NotifyError();
  return false;
}

bool Connector::Accept(Message* message) {
  if (error_)
    return false;

  ZX_ASSERT(channel_);
  if (drop_writes_)
    return true;

  zx_status_t status = WriteMessage(channel_, message);

  if (status == ZX_OK)
    return true;

  if (status == ZX_ERR_BAD_STATE) {
    // There's no point in continuing to write to this channel since the other
    // end is gone. Avoid writing any future messages. Hide write failures
    // from the caller since we'd like them to continue consuming any backlog
    // of incoming messages before regarding the channel as closed.
    drop_writes_ = true;
    return true;
  }

  // This particular write was rejected, presumably because of bad input.
  // The channel is not necessarily in a bad state.
  return false;
}

async_wait_result_t Connector::OnHandleReady(
    async_t* async, zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    NotifyError();
    return ASYNC_WAIT_FINISHED;
  }
  ZX_DEBUG_ASSERT(!error_);

  if (signal->observed & ZX_CHANNEL_READABLE) {
    // Return immediately if |this| was destroyed. Do not touch any members!
    zx_status_t rv;
    for (uint64_t i = 0; i < signal->count; i++) {
      if (!ReadSingleMessage(&rv))
        return ASYNC_WAIT_FINISHED;

      // If we get ZX_ERR_PEER_CLOSED (or another error), we'll already have
      // notified the error and likely been destroyed.
      ZX_DEBUG_ASSERT(rv == ZX_OK || rv == ZX_ERR_SHOULD_WAIT);
      if (rv != ZX_OK)
        break;
    }
    return channel_ ? ASYNC_WAIT_AGAIN : ASYNC_WAIT_FINISHED;
  }

  ZX_DEBUG_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
  // Notice that we don't notify an error until we've drained all the messages
  // out of the channel.
  NotifyError();
  return ASYNC_WAIT_FINISHED;
}

bool Connector::ReadSingleMessage(zx_status_t* read_result) {
  bool receiver_result = false;

  // Detect if |this| was destroyed during message dispatch. Allow for the
  // possibility of re-entering ReadMore() through message dispatch.
  bool was_destroyed_during_dispatch = false;
  bool* previous_destroyed_flag = destroyed_flag_;
  destroyed_flag_ = &was_destroyed_during_dispatch;

  zx_status_t rv =
      ReadAndDispatchMessage(channel_, incoming_receiver_, &receiver_result);
  if (read_result)
    *read_result = rv;

  if (was_destroyed_during_dispatch) {
    if (previous_destroyed_flag)
      *previous_destroyed_flag = true;  // Propagate flag.
    return false;
  }
  destroyed_flag_ = previous_destroyed_flag;

  if (rv == ZX_ERR_SHOULD_WAIT)
    return true;

  if (rv != ZX_OK ||
      (enforce_errors_from_incoming_receiver_ && !receiver_result)) {
    NotifyError();
    return false;
  }
  return true;
}

void Connector::NotifyError() {
  error_ = true;
  CloseChannel();
  if (connection_error_handler_)
    connection_error_handler_();
}

}  // namespace internal
}  // namespace fidl
