// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/internal/message_reader.h"

#include <lib/async/default.h>
#include <lib/fidl/cpp/message_buffer.h>
#include <zircon/assert.h>

namespace fidl {
namespace internal {
namespace {

constexpr zx_signals_t kSignals = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;

// |Canary| is a stack-allocated object that observes when a |MessageReader| is
// destroyed or unbound from the current channel.
//
// Because |WaitAndDispatchOneMessageUntil| can be called re-entrantly, we can
// be in a state where there are N nested calls to |ReadAndDispatchMessage| on
// the stack. While dispatching any of those messages, the client can destroy
// the |MessageReader| or unbind it from the current channel. When that happens
// we need to stop reading messages from the channel and unwind the stack
// safely.
//
// The |Canary| works by storing a pointer to its |should_stop_| field in the
// |MessageReader|.  Upon destruction or unbinding, the |MessageReader| writes
// |true| into |should_stop_|. When we unwind the stack, the |Canary| forwards
// that value to the next |Canary| on the stack.
class Canary {
 public:
  explicit Canary(bool** should_stop_slot)
      : should_stop_slot_(should_stop_slot),
        previous_should_stop_(*should_stop_slot_),
        should_stop_(false) {
    *should_stop_slot_ = &should_stop_;
  }

  ~Canary() {
    if (should_stop_) {
      // If we should stop, we need to propagate that information to the
      // |Canary| higher up the stack, if any. We also cannot touch
      // |*should_stop_slot_| because the |MessageReader| might have been
      // destroyed (or bound to another channel).
      if (previous_should_stop_)
        *previous_should_stop_ = should_stop_;
    } else {
      // Otherwise, the |MessageReader| was not destroyed and is still bound to
      // the same channel. We need to restore the previous |should_stop_|
      // pointer so that a |Canary| further up the stack can still be informed
      // about whether to stop.
      *should_stop_slot_ = previous_should_stop_;
    }
  }

  // Whether the |ReadAndDispatchMessage| that created the |Canary| should stop
  // after dispatching the current message.
  bool should_stop() const { return should_stop_; }

 private:
  bool** should_stop_slot_;
  bool* previous_should_stop_;
  bool should_stop_;
};

}  // namespace

static_assert(std::is_standard_layout<MessageReader>::value,
              "We need offsetof to work");

MessageReader::MessageReader(MessageHandler* message_handler)
    : wait_{{ASYNC_STATE_INIT},
            &MessageReader::CallHandler,
            ZX_HANDLE_INVALID,
            kSignals},
      dispatcher_(nullptr),
      should_stop_(nullptr),
      message_handler_(message_handler) {}

MessageReader::~MessageReader() {
  Stop();
  if (dispatcher_)
    async_cancel_wait(dispatcher_, &wait_);
}

zx_status_t MessageReader::Bind(zx::channel channel,
                                async_dispatcher_t* dispatcher) {
  if (is_bound())
    Unbind();
  if (!channel)
    return ZX_OK;
  channel_ = std::move(channel);
  if (dispatcher) {
    dispatcher_ = dispatcher;
  } else {
    dispatcher_ = async_get_default_dispatcher();
  }
  ZX_ASSERT_MSG(dispatcher_ != nullptr,
                "either |dispatcher| must be non-null, or "
                "|async_get_default_dispatcher| must "
                "be configured to return a non-null vaule");
  wait_.object = channel_.get();
  zx_status_t status = async_begin_wait(dispatcher_, &wait_);
  if (status != ZX_OK)
    Unbind();
  return status;
}

zx::channel MessageReader::Unbind() {
  if (!is_bound())
    return zx::channel();
  Stop();
  async_cancel_wait(dispatcher_, &wait_);
  wait_.object = ZX_HANDLE_INVALID;
  dispatcher_ = nullptr;
  zx::channel channel = std::move(channel_);
  if (message_handler_)
    message_handler_->OnChannelGone();
  return channel;
}

void MessageReader::Reset() {
  Unbind();
  error_handler_ = nullptr;
}

zx_status_t MessageReader::TakeChannelAndErrorHandlerFrom(
    MessageReader* other) {
  zx_status_t status = Bind(other->Unbind(), other->dispatcher_);
  if (status != ZX_OK)
    return status;
  error_handler_ = std::move(other->error_handler_);
  return ZX_OK;
}

zx_status_t MessageReader::WaitAndDispatchOneMessageUntil(zx::time deadline) {
  if (!is_bound())
    return ZX_ERR_BAD_STATE;
  zx_signals_t pending = ZX_SIGNAL_NONE;
  zx_status_t status = channel_.wait_one(kSignals, deadline, &pending);
  if (status == ZX_ERR_TIMED_OUT)
    return status;
  if (status != ZX_OK) {
    NotifyError();
    return status;
  }

  if (pending & ZX_CHANNEL_READABLE) {
    MessageBuffer buffer;
    return ReadAndDispatchMessage(&buffer);
  }

  ZX_DEBUG_ASSERT(pending & ZX_CHANNEL_PEER_CLOSED);
  NotifyError();
  return ZX_ERR_PEER_CLOSED;
}

void MessageReader::CallHandler(async_dispatcher_t* dispatcher,
                                async_wait_t* wait, zx_status_t status,
                                const zx_packet_signal_t* signal) {
  static_assert(offsetof(MessageReader, wait_) == 0,
                "The wait must be the first member for this cast to be valid.");
  reinterpret_cast<MessageReader*>(wait)->OnHandleReady(dispatcher, status,
                                                        signal);
}

void MessageReader::OnHandleReady(async_dispatcher_t* dispatcher,
                                  zx_status_t status,
                                  const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    NotifyError();
    return;
  }

  if (signal->observed & ZX_CHANNEL_READABLE) {
    MessageBuffer buffer;
    for (uint64_t i = 0; i < signal->count; i++) {
      status = ReadAndDispatchMessage(&buffer);
      // If ReadAndDispatchMessage returns ZX_ERR_STOP, that means the message
      // handler has destroyed this object and we need to unwind without
      // touching |this|.
      if (status == ZX_ERR_SHOULD_WAIT)
        break;
      if (status != ZX_OK)
        return;
    }
    status = async_begin_wait(dispatcher, &wait_);
    if (status != ZX_OK) {
      NotifyError();
    }
    return;
  }

  ZX_DEBUG_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
  // Notice that we don't notify an error until we've drained all the messages
  // out of the channel.
  NotifyError();
}

zx_status_t MessageReader::ReadAndDispatchMessage(MessageBuffer* buffer) {
  Message message = buffer->CreateEmptyMessage();
  zx_status_t status = message.Read(channel_.get(), 0);
  if (status == ZX_ERR_SHOULD_WAIT)
    return status;
  if (status != ZX_OK) {
    NotifyError();
    return status;
  }
  if (!message_handler_)
    return ZX_OK;
  Canary canary(&should_stop_);
  status = message_handler_->OnMessage(std::move(message));
  if (canary.should_stop())
    return ZX_ERR_STOP;
  if (status != ZX_OK)
    NotifyError();
  return status;
}

void MessageReader::NotifyError() {
  Unbind();
  if (error_handler_)
    error_handler_();
}

void MessageReader::Stop() {
  if (should_stop_) {
    *should_stop_ = true;
    should_stop_ = nullptr;
  }
}

}  // namespace internal
}  // namespace fidl
