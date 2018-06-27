// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/p2p_provider/impl/remote_connection.h"

#include <flatbuffers/flatbuffers.h>
#include <lib/fit/function.h>

#include "lib/fxl/logging.h"
#include "peridot/bin/ledger/p2p_provider/impl/envelope_generated.h"
#include "peridot/lib/convert/convert.h"

namespace p2p_provider {
RemoteConnection::RemoteConnection(std::string local_name)
    : local_name_(std::move(local_name)) {
  message_relay_.SetChannelClosedCallback([this] { OnChannelClosed(); });
  message_relay_.SetMessageReceivedCallback(
      [this](std::vector<uint8_t> data) { OnNewMessage(std::move(data)); });
}

void RemoteConnection::Start(zx::channel channel) {
  FXL_DCHECK(!started_);
  started_ = true;
  message_relay_.SetChannel(std::move(channel));
}

void RemoteConnection::SendMessage(fxl::StringView data) {
  message_relay_.SendMessage(std::vector<uint8_t>(data.begin(), data.end()));
}

void RemoteConnection::Disconnect() {
  FXL_DCHECK(started_);
  message_relay_.SetChannelClosedCallback(nullptr);
  message_relay_.CloseChannel();

  if (on_empty_) {
    on_empty_();
  }
}

void RemoteConnection::set_on_empty(fit::closure on_empty) {
  on_empty_ = std::move(on_empty);
}

void RemoteConnection::set_on_close(fit::closure on_close) {
  on_close_ = std::move(on_close);
}

void RemoteConnection::set_on_message(
    fit::function<void(std::vector<uint8_t>)> on_message) {
  on_message_ = std::move(on_message);
}

void RemoteConnection::OnChannelClosed() {
  if (on_close_) {
    on_close_();
  }
  if (on_empty_) {
    auto on_empty = std::move(on_empty_);
    on_empty();
  }
}

void RemoteConnection::OnNewMessage(std::vector<uint8_t> data) {
  FXL_DCHECK(on_message_)
      << "No message handler has been set. We would be dropping messages.";

  if (on_message_) {
    on_message_(std::move(data));
  }
}

}  // namespace p2p_provider
