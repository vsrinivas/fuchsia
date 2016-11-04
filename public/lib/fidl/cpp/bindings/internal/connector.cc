// Copyright 2013 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/internal/connector.h"

#include "lib/ftl/compiler_specific.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"

namespace fidl {
namespace internal {

// ----------------------------------------------------------------------------

Connector::Connector(mx::channel message_pipe, const FidlAsyncWaiter* waiter)
    : waiter_(waiter),
      message_pipe_(std::move(message_pipe)),
      incoming_receiver_(nullptr),
      async_wait_id_(0),
      error_(false),
      drop_writes_(false),
      enforce_errors_from_incoming_receiver_(true),
      destroyed_flag_(nullptr) {
  // Even though we don't have an incoming receiver, we still want to monitor
  // the message pipe to know if is closed or encounters an error.
  WaitToReadMore();
}

Connector::~Connector() {
  if (destroyed_flag_)
    *destroyed_flag_ = true;

  CancelWait();
}

void Connector::CloseMessagePipe() {
  CancelWait();
  message_pipe_.reset();
}

mx::channel Connector::PassMessagePipe() {
  CancelWait();
  return std::move(message_pipe_);
}

bool Connector::WaitForIncomingMessage(ftl::TimeDelta timeout) {
  if (error_)
    return false;

  mx_status_t rv = message_pipe_.wait_one(MX_SIGNAL_READABLE,
                                          timeout.ToNanoseconds(), nullptr);
  if (rv == ERR_SHOULD_WAIT || rv == ERR_TIMED_OUT)
    return false;
  if (rv != NO_ERROR) {
    NotifyError();
    return false;
  }
  bool ok = ReadSingleMessage(&rv);
  FTL_ALLOW_UNUSED_LOCAL(ok);
  return (rv == NO_ERROR);
}

bool Connector::Accept(Message* message) {
  if (error_)
    return false;

  FTL_CHECK(message_pipe_);
  if (drop_writes_)
    return true;

  mx_status_t rv = message_pipe_.write(
      0, message->data(), message->data_num_bytes(),
      message->mutable_handles()->empty()
          ? nullptr
          : reinterpret_cast<const mx_handle_t*>(
                &message->mutable_handles()->front()),
      static_cast<uint32_t>(message->mutable_handles()->size()));

  switch (rv) {
    case NO_ERROR:
      // The handles were successfully transferred, so we don't need the message
      // to track their lifetime any longer.
      message->mutable_handles()->clear();
      break;
    case ERR_BAD_STATE:
      // There's no point in continuing to write to this pipe since the other
      // end is gone. Avoid writing any future messages. Hide write failures
      // from the caller since we'd like them to continue consuming any backlog
      // of incoming messages before regarding the message pipe as closed.
      drop_writes_ = true;
      break;
    default:
      // This particular write was rejected, presumably because of bad input.
      // The pipe is not necessarily in a bad state.
      return false;
  }
  return true;
}

// static
void Connector::CallOnHandleReady(mx_status_t result,
                                  mx_signals_t pending,
                                  void* closure) {
  Connector* self = static_cast<Connector*>(closure);
  self->OnHandleReady(result, pending);
}

void Connector::OnHandleReady(mx_status_t result, mx_signals_t pending) {
  FTL_CHECK(async_wait_id_ != 0);
  async_wait_id_ = 0;
  if (result != NO_ERROR) {
    NotifyError();
    return;
  }
  FTL_DCHECK(!error_);

  if (pending & MX_SIGNAL_READABLE) {
    // If the channel is readable, we drain one message out of the channel and
    // then return to the event loop to avoid starvation.

    // Return immediately if |this| was destroyed. Do not touch any members!
    mx_status_t rv;
    if (!ReadSingleMessage(&rv))
      return;

    // If we get ERR_REMOTE_CLOSED (or another error), we'll already have
    // notified the error and likely been destroyed.
    FTL_DCHECK(rv == NO_ERROR || rv == ERR_SHOULD_WAIT);
    WaitToReadMore();

  } else if (pending & MX_SIGNAL_PEER_CLOSED) {
    // Notice that we don't notify an error until we've drained all the messages
    // out of the channel.
    NotifyError();
    // We're likely to be destroyed at this point.
  }
}

void Connector::WaitToReadMore() {
  FTL_CHECK(!async_wait_id_);
  async_wait_id_ = waiter_->AsyncWait(
      message_pipe_.get(), MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED,
      MX_TIME_INFINITE, &Connector::CallOnHandleReady, this);
}

bool Connector::ReadSingleMessage(mx_status_t* read_result) {
  bool receiver_result = false;

  // Detect if |this| was destroyed during message dispatch. Allow for the
  // possibility of re-entering ReadMore() through message dispatch.
  bool was_destroyed_during_dispatch = false;
  bool* previous_destroyed_flag = destroyed_flag_;
  destroyed_flag_ = &was_destroyed_during_dispatch;

  mx_status_t rv = ReadAndDispatchMessage(message_pipe_, incoming_receiver_,
                                          &receiver_result);
  if (read_result)
    *read_result = rv;

  if (was_destroyed_during_dispatch) {
    if (previous_destroyed_flag)
      *previous_destroyed_flag = true;  // Propagate flag.
    return false;
  }
  destroyed_flag_ = previous_destroyed_flag;

  if (rv == ERR_SHOULD_WAIT)
    return true;

  if (rv != NO_ERROR ||
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
  CloseMessagePipe();
  if (connection_error_handler_)
    connection_error_handler_();
}

}  // namespace internal
}  // namespace fidl
