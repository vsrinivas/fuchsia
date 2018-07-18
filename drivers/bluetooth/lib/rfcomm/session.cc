// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/rfcomm/session.h"

namespace btlib {
namespace rfcomm {

std::unique_ptr<Session> Session::Create(
    fbl::RefPtr<l2cap::Channel> l2cap_channel,
    ChannelOpenedCallback channel_opened_cb, async_dispatcher_t* dispatcher) {
  auto session = std::unique_ptr<Session>(
      new Session(std::move(channel_opened_cb), dispatcher));
  if (!session->SetL2CAPChannel(l2cap_channel))
    return nullptr;
  return session;
}

Session::Session(ChannelOpenedCallback channel_opened_cb,
                 async_dispatcher_t* dispatcher)
    : channel_opened_cb_(std::move(channel_opened_cb)),
      dispatcher_(dispatcher),
      weak_ptr_factory_(this) {}

bool Session::SetL2CAPChannel(fbl::RefPtr<l2cap::Channel> l2cap_channel) {
  FXL_DCHECK(!l2cap_channel_);
  FXL_DCHECK(l2cap_channel);
  l2cap_channel_.Reset(l2cap_channel);
  auto self = weak_ptr_factory_.GetWeakPtr();
  return l2cap_channel_->Activate(
      [self](const auto& sdu) {
        if (self)
          self->RxCallback(sdu);
      },
      [self]() {
        if (self)
          self->ClosedCallback();
      },
      dispatcher_);
}

void Session::RxCallback(const l2cap::SDU& sdu) { FXL_NOTIMPLEMENTED(); }

void Session::ClosedCallback() {
  FXL_LOG(INFO) << "Closing session";
  // Deactivates the channel.
  l2cap_channel_ = nullptr;
}

}  // namespace rfcomm
}  // namespace btlib
