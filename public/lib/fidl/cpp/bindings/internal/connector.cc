// Copyright 2013 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/internal/connector.h"

#include "lib/fxl/compiler_specific.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

#include <mx/time.h>

namespace fidl {
namespace internal {

// ----------------------------------------------------------------------------

Connector::Connector(mx::channel channel, const FidlAsyncWaiter* waiter)
    : waiter_(waiter),
      channel_(std::move(channel)),
      incoming_receiver_(nullptr),
      async_wait_id_(0),
      error_(false),
      drop_writes_(false),
      enforce_errors_from_incoming_receiver_(true),
      destroyed_flag_(nullptr) {
  // Even though we don't have an incoming receiver, we still want to monitor
  // the channel to know if is closed or encounters an error.
  WaitToReadMore();
}

Connector::~Connector() {
  if (destroyed_flag_)
    *destroyed_flag_ = true;

  CancelWait();
}

void Connector::CloseChannel() {
  CancelWait();
  channel_.reset();
}

mx::channel Connector::PassChannel() {
  CancelWait();
  return std::move(channel_);
}

bool Connector::WaitForIncomingMessage(fxl::TimeDelta timeout) {
  if (error_)
    return false;

  mx_signals_t pending = MX_SIGNAL_NONE;
  mx_status_t rv = channel_.wait_one(MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED,
                                     timeout == fxl::TimeDelta::Max()
                                         ? MX_TIME_INFINITE
                                         : mx::deadline_after(timeout.ToNanoseconds()),
                                     &pending);
  if (rv == MX_ERR_SHOULD_WAIT || rv == MX_ERR_TIMED_OUT)
    return false;
  if (rv != MX_OK) {
    NotifyError();
    return false;
  }
  if (pending & MX_CHANNEL_READABLE) {
    bool ok = ReadSingleMessage(&rv);
    FXL_ALLOW_UNUSED_LOCAL(ok);
    return (rv == MX_OK);
  }

  FXL_DCHECK(pending & MX_CHANNEL_PEER_CLOSED);
  NotifyError();
  return false;
}

bool Connector::Accept(Message* message) {
  if (error_)
    return false;

  FXL_CHECK(channel_);
  if (drop_writes_)
    return true;

  mx_status_t rv =
      channel_.write(0, message->data(), message->data_num_bytes(),
                     message->mutable_handles()->empty()
                         ? nullptr
                         : reinterpret_cast<const mx_handle_t*>(
                               &message->mutable_handles()->front()),
                     static_cast<uint32_t>(message->mutable_handles()->size()));

  switch (rv) {
    case MX_OK:
      // The handles were successfully transferred, so we don't need the message
      // to track their lifetime any longer.
      message->mutable_handles()->clear();
      break;
    case MX_ERR_BAD_STATE:
      // There's no point in continuing to write to this channel since the other
      // end is gone. Avoid writing any future messages. Hide write failures
      // from the caller since we'd like them to continue consuming any backlog
      // of incoming messages before regarding the channel as closed.
      drop_writes_ = true;
      break;
    default:
      // This particular write was rejected, presumably because of bad input.
      // The channel is not necessarily in a bad state.
      return false;
  }
  return true;
}

// static
void Connector::CallOnHandleReady(mx_status_t result,
                                  mx_signals_t pending,
                                  uint64_t count,
                                  void* closure) {
  Connector* self = static_cast<Connector*>(closure);
  self->OnHandleReady(result, pending, count);
}

void Connector::OnHandleReady(mx_status_t result,
                              mx_signals_t pending,
                              uint64_t count) {
  FXL_CHECK(async_wait_id_ != 0);
  async_wait_id_ = 0;
  if (result != MX_OK) {
    NotifyError();
    return;
  }
  FXL_DCHECK(!error_);

  if (pending & MX_CHANNEL_READABLE) {
    // Return immediately if |this| was destroyed. Do not touch any members!
    mx_status_t rv;
    for (uint64_t i = 0; i < count; i++) {
      if (!ReadSingleMessage(&rv))
        return;

      // If we get MX_ERR_PEER_CLOSED (or another error), we'll already have
      // notified the error and likely been destroyed.
      FXL_DCHECK(rv == MX_OK || rv == MX_ERR_SHOULD_WAIT);
      if (rv != MX_OK) {
        break;
      }
    }
    WaitToReadMore();

  } else if (pending & MX_CHANNEL_PEER_CLOSED) {
    // Notice that we don't notify an error until we've drained all the messages
    // out of the channel.
    NotifyError();
    // We're likely to be destroyed at this point.
  }
}

void Connector::WaitToReadMore() {
  FXL_CHECK(!async_wait_id_);
  async_wait_id_ = waiter_->AsyncWait(
      channel_.get(), MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED,
      MX_TIME_INFINITE, &Connector::CallOnHandleReady, this);
}

bool Connector::ReadSingleMessage(mx_status_t* read_result) {
  bool receiver_result = false;

  // Detect if |this| was destroyed during message dispatch. Allow for the
  // possibility of re-entering ReadMore() through message dispatch.
  bool was_destroyed_during_dispatch = false;
  bool* previous_destroyed_flag = destroyed_flag_;
  destroyed_flag_ = &was_destroyed_during_dispatch;

  mx_status_t rv =
      ReadAndDispatchMessage(channel_, incoming_receiver_, &receiver_result);
  if (read_result)
    *read_result = rv;

  if (was_destroyed_during_dispatch) {
    if (previous_destroyed_flag)
      *previous_destroyed_flag = true;  // Propagate flag.
    return false;
  }
  destroyed_flag_ = previous_destroyed_flag;

  if (rv == MX_ERR_SHOULD_WAIT)
    return true;

  if (rv != MX_OK ||
      (enforce_errors_from_incoming_receiver_ && !receiver_result)) {
    NotifyError();
    return false;
  }
  return true;
}

void Connector::CancelWait() {
  if (!async_wait_id_)
    return;

  waiter_->CancelWait(async_wait_id_);
  async_wait_id_ = 0;
}

void Connector::NotifyError() {
  error_ = true;
  CloseChannel();
  if (connection_error_handler_)
    connection_error_handler_();
}

}  // namespace internal
}  // namespace fidl
