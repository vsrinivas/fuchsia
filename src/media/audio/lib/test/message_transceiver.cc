// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/test/message_transceiver.h"

#include <lib/async/default.h>

namespace media::audio::test {

MessageTransceiver::MessageTransceiver(){};

MessageTransceiver::~MessageTransceiver() { Close(); };

zx_status_t MessageTransceiver::Init(zx::channel channel,
                                     IncomingMessageCallback incoming_message_callback,
                                     ErrorCallback error_callback) {
  channel_ = std::move(channel);
  incoming_message_callback_ = std::move(incoming_message_callback);
  error_callback_ = std::move(error_callback);

  read_wait_.set_object(channel_.get());
  read_wait_.set_trigger(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);

  write_wait_.set_object(channel_.get());
  write_wait_.set_trigger(ZX_CHANNEL_WRITABLE | ZX_CHANNEL_PEER_CLOSED);

  return read_wait_.Begin(async_get_default_dispatcher());
}

void MessageTransceiver::Close() {
  read_wait_.Cancel();
  write_wait_.Cancel();
  channel_.reset();
  incoming_message_callback_ = nullptr;
  error_callback_ = nullptr;
}

zx_status_t MessageTransceiver::SendMessage(Message message) {
  if (!channel_) {
    return ZX_ERR_NOT_CONNECTED;
  }

  outbound_messages_.push(std::move(message));

  if (channel_ && !write_wait_.is_pending()) {
    WriteChannelMessages(async_get_default_dispatcher(), &write_wait_, ZX_OK, nullptr);
  }

  return ZX_OK;
}

void MessageTransceiver::OnError(zx_status_t status) {
  if (error_callback_) {
    error_callback_(status);
  }

  Close();
}

void MessageTransceiver::ReadChannelMessages(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                             zx_status_t status, const zx_packet_signal_t* signal) {
  while (channel_) {
    uint32_t actual_byte_count;
    uint32_t actual_handle_count;
    zx_status_t status =
        channel_.read(0, nullptr, nullptr, 0, 0, &actual_byte_count, &actual_handle_count);

    if (status == ZX_ERR_SHOULD_WAIT) {
      status = wait->Begin(dispatcher);
      if (status != ZX_OK) {
        FXL_PLOG(ERROR, status) << "async::WaitMethod::Begin failed";
        OnError(status);
      }
      break;
    }

    if (status == ZX_ERR_PEER_CLOSED) {
      // Remote end of the channel closed.
      OnError(status);
      break;
    }

    if (status != ZX_ERR_BUFFER_TOO_SMALL) {
      FXL_PLOG(ERROR, status) << "Failed to read (peek) from a zx::channel";
      OnError(status);
      break;
    }

    Message message(actual_byte_count, actual_handle_count);
    status = channel_.read(0, message.bytes_.data(), message.handles_.data(), message.bytes_.size(),
                           message.handles_.size(), &actual_byte_count, &actual_handle_count);

    if (status != ZX_OK) {
      FXL_PLOG(ERROR, status) << "zx::channel::read failed";
      OnError(status);
      break;
    }

    FXL_CHECK(message.bytes_.size() == actual_byte_count);
    FXL_CHECK(message.handles_.size() == actual_handle_count);

    if (incoming_message_callback_) {
      incoming_message_callback_(std::move(message));
    }
  }
}

void MessageTransceiver::WriteChannelMessages(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                              zx_status_t status,
                                              const zx_packet_signal_t* signal) {
  if (!channel_) {
    return;
  }

  while (!outbound_messages_.empty()) {
    const Message& message = outbound_messages_.front();

    zx_status_t status = channel_.write(0, message.bytes_.data(), message.bytes_.size(),
                                        message.handles_.data(), message.handles_.size());

    if (status == ZX_ERR_SHOULD_WAIT) {
      status = wait->Begin(dispatcher);
      if (status != ZX_OK) {
        FXL_PLOG(ERROR, status) << "async::WaitMethod::Begin failed";
        OnError(status);
      }
      break;
    }

    if (status == ZX_ERR_PEER_CLOSED) {
      // Remote end of the channel closed.
      OnError(status);
      break;
    }

    if (status != ZX_OK) {
      FXL_PLOG(ERROR, status) << "zx::channel::write failed";
      OnError(status);
      break;
    }

    outbound_messages_.pop();
  }
}

}  // namespace media::audio::test
