// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/rfcomm/session.h"
#include "garnet/drivers/bluetooth/lib/common/slab_allocator.h"

namespace btlib {
namespace rfcomm {

namespace {

// Timeout system parameters (see RFCOMM 5.3, table 5.1)
//
// T1: timeout for (most) command frames; in RFCOMM, this only applies to SABM
// and DISC frames. 10-60 seconds, recommended value 20 seconds.
constexpr zx::duration kAcknowledgementTimer = zx::sec(20);

// T1': timeout for SABM frames used to start DLCs with DLCI > 0. See RFCOMM
// 5.3. 60-300 seconds.
constexpr zx::duration kAcknowledgementTimerUserDLCs = zx::sec(300);

// The amount of time the multiplexer will wait when a startup conflict is
// detected. A conflict occurs when the local and remote multiplexers attempt to
// start at the same time. After delaying by the below amount, the local
// multiplexer will attempt to start up the multiplexer again. see RFCOMM 5.2.1.
constexpr zx::duration kMuxStartupConflictDelay = zx::msec(20);

}  // namespace

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

void Session::OpenRemoteChannel(ServerChannel server_channel,
                                ChannelOpenedCallback channel_opened_cb) {
  if (!multiplexer_started()) {
    tasks_pending_mux_startup_.emplace(
        [this, server_channel, cb = std::move(channel_opened_cb)]() mutable {
          OpenRemoteChannel(server_channel, std::move(cb));
        });
    StartupMultiplexer();
    return;
  }

  // TODO(NET-1288): implement initial parameter negotiation

  // TODO(gusss): implement channel open

  // Return a nullptr channel if we fail to open
  FXL_LOG(WARNING) << "Not implemented";
  async::PostTask(dispatcher_, [cb = std::move(channel_opened_cb)] {
    cb(nullptr, kInvalidServerChannel);
  });
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
      case FrameType::kSetAsynchronousBalancedMode:
        HandleSABM(dlci);
        return;

      case FrameType::kUnnumberedAcknowledgement:
      case FrameType::kDisconnectedMode: {
        auto callbacks_it = outstanding_frames_.find(dlci);
        if (callbacks_it == outstanding_frames_.end()) {
          FXL_NOTIMPLEMENTED() << "Unsolicited UA or DM frame";
          return;
        }

        // Cancel the timeout and run the callback.
        callbacks_it->second.second->Cancel();
        async::PostTask(
            dispatcher_,
            [cb = std::move(callbacks_it->second.first),
             fr = std::move(frame)]() mutable { cb(std::move(fr)); });

        outstanding_frames_.erase(dlci);

        return;
      }

      default:
        // TODO(gusss): implement better error handling here.
        FXL_LOG(WARNING) << "rfcomm: Unrecognized frame type received: "
                         << (unsigned)frame->control();
        return;
    }
  });
}

void Session::ClosedCallback() { Closedown(); }

bool Session::SendFrame(std::unique_ptr<Frame> frame,
                        FrameResponseCallback callback) {
  const bool expecting_response = static_cast<bool>(callback);
  const DLCI dlci = frame->dlci();
  const FrameType type = static_cast<FrameType>(frame->control());

  // If the multiplexer isn't started, only startup frames should be sent.
  FXL_DCHECK(multiplexer_started() || IsMuxStartupFrame(type, dlci));

  // TODO(gusss): check that the DLC is actually open.

  // TODO(NET-1079, NET-1080): check flow control and queue the frame if it
  // needs to be queued.

  // TODO(gusss): attach credits to frame.

  // Allocate and write the buffer.
  auto buffer = common::NewSlabBuffer(frame->written_size());
  if (!buffer) {
    FXL_LOG(WARNING) << "rfcomm: Couldn't allocate frame buffer ("
                     << frame->written_size() << ")";
    return false;
  }
  frame->Write(buffer->mutable_view());

  // If we're expecting a frame-level response, store the callbacks
  if (expecting_response) {
    if (outstanding_frames_.find(dlci) != outstanding_frames_.end()) {
      FXL_LOG(WARNING)
          << "rfcomm: Drop frame, outstanding command frame for DLCI "
          << static_cast<unsigned>(dlci);
      return false;
    }

    // Make timeout callback
    auto timeout_cb = std::make_unique<async::TaskClosure>([this, dlci] {
      FXL_LOG(ERROR) << "rfcomm: Outstanding frame on DLCI "
                     << static_cast<unsigned>(dlci)
                     << " timed out; closing down session";
      Closedown();
    });

    // Set response and timeout callbacks
    FrameResponseCallbacks callbacks = {
        std::move(callback),
        std::move(timeout_cb),
    };

    // Start timeout callback. A different timeout is used if this is a SABM
    // command on a user data channel (RFCOMM 5.3).
    zx::duration timeout = (type == FrameType::kSetAsynchronousBalancedMode &&
                            dlci >= kMinUserDLCI && dlci <= kMaxUserDLCI)
                               ? kAcknowledgementTimerUserDLCs
                               : kAcknowledgementTimer;
    callbacks.second->PostDelayed(dispatcher_, timeout);

    outstanding_frames_.emplace(dlci, std::move(callbacks));
  }

  if (l2cap_channel_->Send(std::move(buffer))) {
    return true;
  } else {
    FXL_VLOG(1) << "rfcomm: Failed to send frame";

    if (expecting_response) {
      // Cancel timeout and remove callbacks
      outstanding_frames_[dlci].second->Cancel();
      outstanding_frames_.erase(dlci);
    }

    return false;
  }
}

void Session::StartupMultiplexer() {
  if (role_ == Role::kNegotiating || multiplexer_started()) {
    FXL_LOG(WARNING) << "rfcomm: StartupMultiplexer when starting or started";
    return;
  }

  FXL_LOG(INFO) << "rfcomm: Starting multiplexer";

  role_ = Role::kNegotiating;

  auto sabm_command = std::make_unique<SetAsynchronousBalancedModeCommand>(
      role_, kMuxControlDLCI);

  bool sent = SendFrame(
      std::move(sabm_command), [this](std::unique_ptr<Frame> response) {
        FrameType type = static_cast<FrameType>(response->control());
        FXL_DCHECK(type == FrameType::kUnnumberedAcknowledgement ||
                   type == FrameType::kDisconnectedMode);

        switch (role_) {
          case Role::kNegotiating: {
            if (type == FrameType::kUnnumberedAcknowledgement) {
              SetMultiplexerStarted(Role::kInitiator);
            } else {
              FXL_LOG(WARNING) << "rfcomm: Remote multiplexer startup refused"
                               << " by remote";
              role_ = Role::kUnassigned;
            }
            return;
          }
          case Role::kUnassigned:
          case Role::kInitiator:
          case Role::kResponder:
            // TODO(guss): should a UA be received in any of these cases?
            FXL_LOG(WARNING) << "rfcomm: Mux UA frame received in unexpected"
                             << " state";
            return;
            break;
          default:
            FXL_NOTREACHED();
            break;
        }
      });
  FXL_DCHECK(sent);
}

void Session::HandleSABM(DLCI dlci) {
  if (dlci == kMuxControlDLCI) {
    // A SABM frame on the mux control DLCI indicates that we should start
    // up the multiplexer.
    switch (role_) {
      case Role::kUnassigned: {
        // We received a SABM request to start the multiplexer. Reply positively
        // to the request, meaning that the peer becomes the initiator and this
        // session becomes the responder.
        auto ua_response =
            std::make_unique<UnnumberedAcknowledgementResponse>(role_, dlci);
        bool sent = SendFrame(std::move(ua_response));
        FXL_DCHECK(sent);
        SetMultiplexerStarted(Role::kResponder);
        return;
      }
      case Role::kNegotiating: {
        // In this case, we have an outstanding request to start the
        // multiplexer. Respond negatively and attempt startup again later. See
        // RFCOMM 5.2.1.

        FXL_LOG(INFO) << "rfcomm: Resolving multiplexer startup conflict";

        // "Undo" our multiplexer startup request by changing our role back,
        // cancelling timeout, and removing callbacks.
        role_ = Role::kUnassigned;
        auto frame_it = outstanding_frames_.find(dlci);
        FXL_DCHECK(frame_it != outstanding_frames_.end());
        frame_it->second.second->Cancel();
        outstanding_frames_.erase(frame_it);

        auto dm_response =
            std::make_unique<DisconnectedModeResponse>(role_, kMuxControlDLCI);
        bool sent = SendFrame(std::move(dm_response));
        FXL_DCHECK(sent);
        async::PostDelayedTask(
            dispatcher_,
            [this] {
              if (!multiplexer_started()) {
                FXL_LOG(INFO) << "rfcomm: Retrying multiplexer startup";
                StartupMultiplexer();
              }
            },
            kMuxStartupConflictDelay);
        return;
      }
      case Role::kInitiator:
      case Role::kResponder:
        // TODO(gusss): should we send a DM in this case?
        FXL_LOG(WARNING) << "rfcomm: Request to start alreadys started"
                         << " multiplexer";
        return;
      default:
        FXL_NOTREACHED();
        return;
    }
  } else {
    // TODO(NET-1014): open channel when requested by remote peer
    FXL_NOTIMPLEMENTED();
  }
}

void Session::SetMultiplexerStarted(Role role) {
  FXL_DCHECK(role == Role::kInitiator || role == Role::kResponder);

  role_ = role;
  FXL_LOG(INFO) << "rfcomm: Multiplexer started. Role: "
                << (role == Role::kInitiator ? "initiator" : "responder");

  // Run any pending tasks.
  while (!tasks_pending_mux_startup_.empty()) {
    async::PostTask(dispatcher_, std::move(tasks_pending_mux_startup_.front()));
    tasks_pending_mux_startup_.pop();
  }

  // TODO(gusss): send frames from queue when queueing implemented
}

void Session::Closedown() {
  FXL_LOG(INFO) << "rfcomm: Closing session";
  // Deactivates the channel.
  l2cap_channel_ = nullptr;
}

}  // namespace rfcomm
}  // namespace btlib
