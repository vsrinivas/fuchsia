// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_P2P_PROVIDER_IMPL_REMOTE_CONNECTION_H_
#define SRC_LEDGER_BIN_P2P_PROVIDER_IMPL_REMOTE_CONNECTION_H_

#include <lib/fit/function.h>
#include <lib/zx/channel.h>

#include <string>

#include "src/ledger/bin/fidl_helpers/message_relay.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace p2p_provider {
// RemoteConnection holds a connection with a single remote device.
class RemoteConnection {
 public:
  RemoteConnection();

  // Starts listening on the provided channel for new messages.
  // |channel| is presumed to be sending/receiving messages from/to another
  // device.
  void Start(zx::channel channel);

  // Sends |data| to another device through the channel set in |Start|.
  void SendMessage(absl::string_view data);

  // Disconnects.
  void Disconnect();

  // |on_discardable| will be called when this connection is no longer valid, either
  // because we disconnected or because the other side disconnected.
  void SetOnDiscardable(fit::closure on_discardable);

  bool IsDiscardable() const;

  // |on_close| will be called when the other side closes the connection.
  void set_on_close(fit::closure on_close);

  // |on_message| will be called for every new message received.
  void set_on_message(fit::function<void(std::vector<uint8_t>)> on_message);

 private:
  void OnChannelClosed();
  void OnNewMessage(std::vector<uint8_t> data);

  bool started_ = false;

  ledger::fidl_helpers::MessageRelay message_relay_;

  fit::closure on_discardable_;
  fit::closure on_close_;
  fit::function<void(std::vector<uint8_t>)> on_message_;
};

}  // namespace p2p_provider

#endif  // SRC_LEDGER_BIN_P2P_PROVIDER_IMPL_REMOTE_CONNECTION_H_
