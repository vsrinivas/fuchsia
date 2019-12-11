// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/p2p_provider/impl/remote_connection.h"

#include <lib/fit/function.h>

#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/lib/fxl/logging.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

namespace p2p_provider {
RemoteConnection::RemoteConnection() {
  message_relay_.SetChannelClosedCallback([this] { OnChannelClosed(); });
  message_relay_.SetMessageReceivedCallback(
      [this](std::vector<uint8_t> data) { OnNewMessage(std::move(data)); });
}

void RemoteConnection::Start(zx::channel channel) {
  LEDGER_DCHECK(!started_);
  started_ = true;
  message_relay_.SetChannel(std::move(channel));
}

void RemoteConnection::SendMessage(absl::string_view data) {
  message_relay_.SendMessage(std::vector<uint8_t>(data.begin(), data.end()));
}

void RemoteConnection::Disconnect() {
  LEDGER_DCHECK(started_);
  message_relay_.SetChannelClosedCallback(nullptr);
  message_relay_.CloseChannel();

  if (on_discardable_) {
    on_discardable_();
  }
}

void RemoteConnection::SetOnDiscardable(fit::closure on_discardable) {
  on_discardable_ = std::move(on_discardable);
}

bool RemoteConnection::IsDiscardable() const { return message_relay_.IsClosed(); }

void RemoteConnection::set_on_close(fit::closure on_close) { on_close_ = std::move(on_close); }

void RemoteConnection::set_on_message(fit::function<void(std::vector<uint8_t>)> on_message) {
  on_message_ = std::move(on_message);
}

void RemoteConnection::OnChannelClosed() {
  auto on_close = std::move(on_close_);
  auto on_discardable = std::move(on_discardable_);

  if (on_close) {
    on_close();
  }
  if (on_discardable) {
    on_discardable();
  }
}

void RemoteConnection::OnNewMessage(std::vector<uint8_t> data) {
  LEDGER_DCHECK(on_message_) << "No message handler has been set. We would be dropping messages.";

  if (on_message_) {
    on_message_(std::move(data));
  }
}

}  // namespace p2p_provider
