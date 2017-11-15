// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <async/auto_wait.h>
#include <zx/channel.h>

#include <queue>
#include <vector>

#include "lib/fxl/macros.h"

namespace netconnector {

// Moves data-only (no handles) messages across an zx::channel. This is an
// abstract base class with overridables for message arrival and channel
// closure. Use MessageRelay if you prefer to set callbacks for those things.
//
// MessageRelayBase is not thread-safe. All methods calls must be serialized.
class MessageRelayBase {
 public:
  virtual ~MessageRelayBase();

  // Sets the channel that the relay should use to move messages.
  void SetChannel(zx::channel channel);

  // Sends a message.
  void SendMessage(std::vector<uint8_t> message);

  // Closes the channel.
  void CloseChannel();

 protected:
  MessageRelayBase();

  // Called when a message is received.
  virtual void OnMessageReceived(std::vector<uint8_t> message) = 0;

  // Called when the channel closes.
  virtual void OnChannelClosed() = 0;

 private:
  // Tries to read messages from channel_ and waits for more.
  async_wait_result_t ReadChannelMessages(
    async_t* async, zx_status_t status,
    const zx_packet_signal_t* signal);

  // Writes all the messages in messages_to_write_.
  async_wait_result_t WriteChannelMessages(
    async_t* async, zx_status_t status,
    const zx_packet_signal_t* signal);

  zx::channel channel_;
  async::AutoWait read_wait_;
  async::AutoWait write_wait_;
  std::queue<std::vector<uint8_t>> messages_to_write_;
};

// Moves data-only (no handles) messages across an zx::channel.
//
// MessageRelay is not thread-safe. All methods calls must be serialized.
class MessageRelay : public MessageRelayBase {
 public:
  MessageRelay();

  ~MessageRelay() override;

  void SetMessageReceivedCallback(
      std::function<void(std::vector<uint8_t>)> callback);

  void SetChannelClosedCallback(std::function<void()> callback);

 protected:
  void OnMessageReceived(std::vector<uint8_t> message) override;

  void OnChannelClosed() override;

 private:
  std::function<void(std::vector<uint8_t>)> message_received_callback_;
  std::function<void()> channel_closed_callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MessageRelay);
};

}  // namespace netconnector
