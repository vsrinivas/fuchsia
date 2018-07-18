// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/rfcomm/session.h"
#include "garnet/drivers/bluetooth/lib/common/slab_allocator.h"

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
    : role_(Role::kUnassigned),
      channel_opened_cb_(std::move(channel_opened_cb)),
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

void Session::RxCallback(const l2cap::SDU& sdu) {
  l2cap::PDU::Reader reader(&sdu);
  reader.ReadNext(sdu.length(), [&](const common::ByteBuffer& buffer) {
    auto frame = Frame::Parse(credit_based_flow_, role_, buffer);
    if (!frame) {
      FXL_LOG(ERROR) << "Could not parse frame";
      return;
    }

    DLCI dlci = frame->dlci();

    switch ((FrameType)frame->control()) {
      case FrameType::kSetAsynchronousBalancedMode: {
        // The only response we will implement right now is a negative response
        // to the multiplexer startup command.
        if (!multiplexer_started() && dlci == kMuxControlDLCI) {
          SendFrame(std::make_unique<DisconnectedModeResponse>(
              role_, kMuxControlDLCI));
          return;
        }

        // TODO(NET-1014): open channels when requested by peer
        FXL_NOTIMPLEMENTED();
      }
      default:
        // TODO(gusss): implement better error handling here.
        FXL_LOG(WARNING) << "rfcomm: Unrecognized frame type received: "
                         << (unsigned)frame->control();
        return;
    }
  });
}

void Session::ClosedCallback() {
  FXL_LOG(INFO) << "rfcomm: Closing session";
  // Deactivates the channel.
  l2cap_channel_ = nullptr;
}

void Session::SendFrame(std::unique_ptr<Frame> frame) {
  // TODO(NET-1161): once multiplexer startup is implemented, check that the
  // multiplexer is started.

  // TODO(gusss): check that the DLC is actually open.

  // TODO(NET-1079, NET-1080): check flow control and queue the frame if it
  // needs to be queued.

  // TODO(gusss): attach credits to frame.

  // Allocate and write the buffer.
  auto buffer = common::NewSlabBuffer(frame->written_size());
  if (!buffer) {
    FXL_LOG(WARNING) << "rfcomm: Couldn't allocate frame buffer ("
                     << frame->written_size() << ")";
    return;
  }
  frame->Write(buffer->mutable_view());

  if (!l2cap_channel_->Send(std::move(buffer))) {
    FXL_LOG(ERROR) << "Failed to send frame";
  }
}

}  // namespace rfcomm
}  // namespace btlib
