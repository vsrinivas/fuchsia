// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_NETCONNECTOR_CPP_MESSAGE_RELAY_H_
#define LIB_NETCONNECTOR_CPP_MESSAGE_RELAY_H_

#include <lib/async/cpp/wait.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>

#include <queue>
#include <vector>

#include "garnet/public/lib/callback/destruction_sentinel.h"
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
  void ReadChannelMessages(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                           zx_status_t status,
                           const zx_packet_signal_t* signal);

  // Writes all the messages in messages_to_write_.
  void WriteChannelMessages(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                            zx_status_t status,
                            const zx_packet_signal_t* signal);

  zx::channel channel_;
  async::WaitMethod<MessageRelayBase, &MessageRelayBase::ReadChannelMessages>
      read_wait_{this};
  async::WaitMethod<MessageRelayBase, &MessageRelayBase::WriteChannelMessages>
      write_wait_{this};
  std::queue<std::vector<uint8_t>> messages_to_write_;

  callback::DestructionSentinel destruction_sentinel_;
};

// Moves data-only (no handles) messages across an zx::channel.
//
// MessageRelay is not thread-safe. All methods calls must be serialized.
class MessageRelay : public MessageRelayBase {
 public:
  MessageRelay();

  ~MessageRelay() override;

  void SetMessageReceivedCallback(
      fit::function<void(std::vector<uint8_t>)> callback);

  void SetChannelClosedCallback(fit::closure callback);

 protected:
  void OnMessageReceived(std::vector<uint8_t> message) override;

  void OnChannelClosed() override;

 private:
  fit::function<void(std::vector<uint8_t>)> message_received_callback_;
  fit::closure channel_closed_callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MessageRelay);
};

}  // namespace netconnector

#endif  // LIB_NETCONNECTOR_CPP_MESSAGE_RELAY_H_
