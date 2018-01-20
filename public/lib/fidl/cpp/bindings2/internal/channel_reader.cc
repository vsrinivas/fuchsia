// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings2/internal/channel_reader.h"

#include <async/default.h>
#include <fidl/cpp/message_buffer.h>
#include <zircon/assert.h>

namespace fidl {
namespace internal {
namespace {

constexpr zx_signals_t kSignals = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED;

} // namespace

ChannelReader::ChannelReader(MessageHandler* message_handler)
  : async_(nullptr),
    wait_(this, ZX_HANDLE_INVALID, kSignals),
    message_handler_(message_handler) {
}

ChannelReader::~ChannelReader() {
  if (async_)
    wait_.Cancel(async_);
}

zx_status_t ChannelReader::Bind(zx::channel channel) {
  if (is_bound())
    Unbind();
  channel_ = std::move(channel);
  async_ = async_get_default();
  wait_.set_object(channel_.get());
  zx_status_t status = wait_.Begin(async_);
  if (status != ZX_OK)
    Unbind();
  return status;
}

zx::channel ChannelReader::Unbind() {
  wait_.Cancel(async_);
  wait_.set_object(ZX_HANDLE_INVALID);
  async_ = nullptr;
  return std::move(channel_);
}

zx_status_t ChannelReader::WaitAndDispatchMessageUntil(zx::time deadline) {
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

async_wait_result_t ChannelReader::OnHandleReady(
    async_t* async,
    zx_status_t status,
    const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    NotifyError();
    return ASYNC_WAIT_FINISHED;
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
        return ASYNC_WAIT_FINISHED;
    }
    return is_bound() ? ASYNC_WAIT_AGAIN : ASYNC_WAIT_FINISHED;
  }

  ZX_DEBUG_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
  // Notice that we don't notify an error until we've drained all the messages
  // out of the channel.
  NotifyError();
  return ASYNC_WAIT_FINISHED;
}

zx_status_t ChannelReader::ReadAndDispatchMessage(MessageBuffer* buffer) {
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
  status = message_handler_->OnMessage(std::move(message));
  if (status != ZX_OK && status != ZX_ERR_STOP)
    NotifyError();
  return status;
}

void ChannelReader::NotifyError() {
  Unbind();
  if (error_handler_)
    error_handler_();
}

} // namespace internal
} // namespace fidl
