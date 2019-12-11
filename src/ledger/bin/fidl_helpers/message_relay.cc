// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/fidl_helpers/message_relay.h"

#include <lib/async/default.h>

#include "src/ledger/lib/logging/logging.h"

namespace ledger {
namespace fidl_helpers {

MessageRelayBase::MessageRelayBase() = default;

MessageRelayBase::~MessageRelayBase() = default;

void MessageRelayBase::SetChannel(zx::channel channel) {
  LEDGER_DCHECK(channel);
  LEDGER_DCHECK(!channel_) << "SetChannel called twice without intervening call to CloseChannel";

  channel_.swap(channel);

  read_wait_.set_object(channel_.get());
  read_wait_.set_trigger(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);

  write_wait_.set_object(channel_.get());
  write_wait_.set_trigger(ZX_CHANNEL_WRITABLE | ZX_CHANNEL_PEER_CLOSED);

  // We defer handling channel messages so that the caller doesn't get callbacks
  // during SetChannel.

  read_wait_.Begin(async_get_default_dispatcher());

  if (!messages_to_write_.empty()) {
    write_wait_.Begin(async_get_default_dispatcher());
  }
}

void MessageRelayBase::SendMessage(std::vector<uint8_t> message) {
  messages_to_write_.push(std::move(message));

  if (channel_ && !write_wait_.is_pending()) {
    WriteChannelMessages(async_get_default_dispatcher(), &write_wait_, ZX_OK, nullptr);
  }
}

void MessageRelayBase::CloseChannel() {
  read_wait_.Cancel();
  write_wait_.Cancel();
  channel_.reset();
  OnChannelClosed();
}

bool MessageRelayBase::IsClosed() const { return static_cast<bool>(channel_); }

void MessageRelayBase::ReadChannelMessages(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                           zx_status_t status, const zx_packet_signal_t* signal) {
  while (channel_) {
    uint32_t actual_byte_count;
    uint32_t actual_handle_count;
    zx_status_t status =
        channel_.read(0, nullptr, nullptr, 0, 0, &actual_byte_count, &actual_handle_count);

    if (status == ZX_ERR_SHOULD_WAIT) {
      status = wait->Begin(dispatcher);
      if (status != ZX_OK) {
        LEDGER_LOG(ERROR) << "Failed to wait on read channel, status " << status;
        CloseChannel();
      }
      break;
    }

    if (status == ZX_ERR_PEER_CLOSED) {
      // Remote end of the channel closed.
      CloseChannel();
      break;
    }

    if (status != ZX_ERR_BUFFER_TOO_SMALL) {
      LEDGER_LOG(ERROR) << "Failed to read (peek) from channel, status " << status;
      CloseChannel();
      break;
    }

    if (actual_handle_count != 0) {
      LEDGER_LOG(ERROR) << "Message received over channel has handles, closing connection";
      CloseChannel();
      break;
    }

    std::vector<uint8_t> message(actual_byte_count);
    status = channel_.read(0, message.data(), nullptr, message.size(), 0, &actual_byte_count,
                           &actual_handle_count);

    if (status != ZX_OK) {
      LEDGER_LOG(ERROR) << "Failed to read from channel, status " << status;
      CloseChannel();
      break;
    }

    LEDGER_DCHECK(actual_byte_count == message.size());

    if (destruction_sentinel_.DestructedWhile(
            [this, &message] { OnMessageReceived(std::move(message)); })) {
      return;
    }
  }
}

void MessageRelayBase::WriteChannelMessages(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                            zx_status_t status, const zx_packet_signal_t* signal) {
  if (!channel_) {
    return;
  }

  while (!messages_to_write_.empty()) {
    const std::vector<uint8_t>& message = messages_to_write_.front();

    zx_status_t status = channel_.write(0, message.data(), message.size(), nullptr, 0);

    if (status == ZX_ERR_SHOULD_WAIT) {
      status = wait->Begin(dispatcher);
      if (status != ZX_OK) {
        LEDGER_LOG(ERROR) << "Failed to wait on write channel, status " << status;
        CloseChannel();
      }
      break;
    }

    if (status == ZX_ERR_PEER_CLOSED) {
      // Remote end of the channel closed.
      CloseChannel();
      break;
    }

    if (status != ZX_OK) {
      LEDGER_LOG(ERROR) << "zx::channel::write failed, status " << status;
      CloseChannel();
      break;
    }

    messages_to_write_.pop();
  }
}

MessageRelay::MessageRelay() = default;

MessageRelay::~MessageRelay() = default;

void MessageRelay::SetMessageReceivedCallback(fit::function<void(std::vector<uint8_t>)> callback) {
  message_received_callback_ = std::move(callback);
}

void MessageRelay::SetChannelClosedCallback(fit::closure callback) {
  channel_closed_callback_ = std::move(callback);
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

}  // namespace fidl_helpers
}  // namespace ledger
