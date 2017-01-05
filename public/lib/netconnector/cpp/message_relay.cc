// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/netconnector/lib/message_relay.h"

#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace netconnector {

MessageRelayBase::MessageRelayBase() {}

MessageRelayBase::~MessageRelayBase() {}

void MessageRelayBase::SetChannel(mx::channel channel) {
  FTL_DCHECK(channel);
  FTL_DCHECK(!channel_)
      << "SetChannel called twice without intervening call to CloseChannel";

  channel_.swap(channel);

  ReadChannelMessages();

  if (!messages_to_write_.empty()) {
    WriteChannelMessages();
  }
}

void MessageRelayBase::SendMessage(std::vector<uint8_t> message) {
  messages_to_write_.push(std::move(message));

  if (channel_ && !write_async_wait_.is_waiting()) {
    WriteChannelMessages();
  }
}

void MessageRelayBase::CloseChannel() {
  channel_.reset();
  OnChannelClosed();
}

void MessageRelayBase::ReadChannelMessages() {
  while (channel_) {
    uint32_t actual_byte_count;
    uint32_t actual_handle_count;
    mx_status_t status = channel_.read(0, nullptr, 0, &actual_byte_count,
                                       nullptr, 0, &actual_handle_count);

    if (status == ERR_SHOULD_WAIT) {
      // Nothing to read. Wait until there is.
      read_async_wait_.Start(
          channel_.get(), MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED,
          MX_TIME_INFINITE, [this]() { ReadChannelMessages(); });
      return;
    }

    if (status == ERR_REMOTE_CLOSED) {
      // Remote end of the channel closed.
      CloseChannel();
      return;
    }

    if (status != ERR_BUFFER_TOO_SMALL) {
      FTL_LOG(ERROR) << "Failed to read (peek) from channel, status " << status;
      CloseChannel();
      return;
    }

    if (actual_handle_count != 0) {
      FTL_LOG(ERROR)
          << "Message received over channel has handles, closing connection";
      CloseChannel();
      return;
    }

    std::vector<uint8_t> message(actual_byte_count);
    status =
        channel_.read(0, message.data(), message.size(), &actual_byte_count,
                      nullptr, 0, &actual_handle_count);

    if (status != NO_ERROR) {
      FTL_LOG(ERROR) << "Failed to read from channel, status " << status;
      CloseChannel();
      return;
    }

    FTL_DCHECK(actual_byte_count == message.size());

    OnMessageReceived(std::move(message));
  }
}

void MessageRelayBase::WriteChannelMessages() {
  if (!channel_) {
    return;
  }

  while (!messages_to_write_.empty()) {
    const std::vector<uint8_t>& message = messages_to_write_.front();

    mx_status_t status =
        channel_.write(0, message.data(), message.size(), nullptr, 0);

    if (status == ERR_SHOULD_WAIT) {
      // No room for the write. Wait until there is.
      write_async_wait_.Start(
          channel_.get(), MX_CHANNEL_WRITABLE | MX_CHANNEL_PEER_CLOSED,
          MX_TIME_INFINITE, [this]() { WriteChannelMessages(); });
      return;
    }

    if (status == ERR_REMOTE_CLOSED) {
      // Remote end of the channel closed.
      CloseChannel();
      return;
    }

    if (status != NO_ERROR) {
      FTL_LOG(ERROR) << "mx::channel::write failed, status " << status;
      CloseChannel();
      return;
    }

    messages_to_write_.pop();
  }
}

MessageRelay::MessageRelay() {}

MessageRelay::~MessageRelay() {}

void MessageRelay::SetMessageReceivedCallback(
    std::function<void(std::vector<uint8_t>)> callback) {
  message_received_callback_ = callback;
}

void MessageRelay::SetChannelClosedCallback(std::function<void()> callback) {
  channel_closed_callback_ = callback;
}

void MessageRelay::OnMessageReceived(std::vector<uint8_t> message) {
  if (message_received_callback_) {
    message_received_callback_(std::move(message));
  }
}

void MessageRelay::OnChannelClosed() {
  if (channel_closed_callback_) {
    channel_closed_callback_();
  }
}

}  // namespace example
