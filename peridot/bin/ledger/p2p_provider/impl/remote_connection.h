// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_P2P_PROVIDER_IMPL_REMOTE_CONNECTION_H_
#define PERIDOT_BIN_LEDGER_P2P_PROVIDER_IMPL_REMOTE_CONNECTION_H_

#include <string>

#include <flatbuffers/flatbuffers.h>
#include <lib/fit/function.h>
#include <lib/fxl/strings/string_view.h>
#include <lib/netconnector/cpp/message_relay.h>
#include <lib/zx/channel.h>

namespace p2p_provider {
// RemoteConnection holds a connection with a single remote device.
class RemoteConnection {
 public:
  explicit RemoteConnection(std::string local_name);

  // Starts listening on the provided channel for new messages.
  // |channel| is presumed to be sending/receiving messages from/to another
  // device.
  void Start(zx::channel channel);

  // Sends |data| to another device through the channel set in |Start|.
  void SendMessage(fxl::StringView data);

  // Disconnects.
  void Disconnect();

  // |on_empty| will be called when this connection is no longer valid, either
  // because we disconnected or because the other side disconnected.
  void set_on_empty(fit::closure on_empty);

  // |on_close| will be called when the other side closes the connection.
  void set_on_close(fit::closure on_close);

  // |on_message| will be called for every new message received.
  void set_on_message(fit::function<void(std::vector<uint8_t>)> on_message);

 private:
  void OnChannelClosed();
  void OnNewMessage(std::vector<uint8_t> data);

  bool started_ = false;

  const std::string local_name_;
  netconnector::MessageRelay message_relay_;

  fit::closure on_empty_;
  fit::closure on_close_;
  fit::function<void(std::vector<uint8_t>)> on_message_;
};

}  // namespace p2p_provider

#endif  // PERIDOT_BIN_LEDGER_P2P_PROVIDER_IMPL_REMOTE_CONNECTION_H_
