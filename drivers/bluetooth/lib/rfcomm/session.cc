// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/default.h>

#include "garnet/drivers/bluetooth/lib/common/slab_allocator.h"
#include "garnet/drivers/bluetooth/lib/rfcomm/rfcomm.h"
#include "garnet/drivers/bluetooth/lib/rfcomm/session.h"

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

// T2: timeout for multiplexer commands. 10-60 seconds, recommended value 20
// seconds.
constexpr zx::duration kMuxResponseTimer = zx::sec(20);

// The amount of time the multiplexer will wait when a startup conflict is
// detected. A conflict occurs when the local and remote multiplexers attempt to
// start at the same time. After delaying by the below amount, the local
// multiplexer will attempt to start up the multiplexer again. see RFCOMM 5.2.1.
constexpr zx::duration kMuxStartupConflictDelay = zx::msec(20);

// Used to indicate that an outstanding multiplexer command does not pertain to
// any specific DLCI. This is applicable for multiplexer commands such as the
// Test command.
static constexpr DLCI kNoDLCI = 1;  // 1 is an invalid DLCI.

// Given a multiplexer command, find the DLCI which this command pertains to.
// Commands such as DLC Parameter Negotiation (PN) pertain to specific DLCs,
// whereas commands like the Test command or FCon/FCoff commands do not.
// Returns kNoDLCI for those commands which do not pertain to a specific DLCI.
DLCI GetDLCIFromMuxCommand(MuxCommand* mux_command) {
  MuxCommandType type = mux_command->command_type();
  switch (type) {
    case MuxCommandType::kDLCParameterNegotiation:
      return static_cast<DLCParameterNegotiationCommand*>(mux_command)
          ->params()
          .dlci;
    case MuxCommandType::kModemStatusCommand:
      return static_cast<ModemStatusCommand*>(mux_command)->dlci();
    case MuxCommandType::kRemoteLineStatusCommand:
      return static_cast<RemoteLineStatusCommand*>(mux_command)->dlci();
    case MuxCommandType::kRemotePortNegotiationCommand:
      return static_cast<RemotePortNegotiationCommand*>(mux_command)->dlci();
    case MuxCommandType::kFlowControlOffCommand:
    case MuxCommandType::kFlowControlOnCommand:
    case MuxCommandType::kTestCommand:
    case MuxCommandType::kNonSupportedCommandResponse:
      return kNoDLCI;
    default:
      FXL_LOG(ERROR) << "Unexpected multiplexer command type";
      return kNoDLCI;
  }
}

// Returns true if this user DLCI "belongs to" the side of the session with the
// given |role|. See RFCOMM 5.2: "...this partitions the DLCI value space such
// that server applications on the non-initiating device are reachable on DLCIs
// 2,4,6,...,60, and server applications on the initiating device are reachable
// on 3,5,7,...,61."
constexpr bool IsValidLocalChannel(Role role, DLCI dlci) {
  FXL_DCHECK(IsMultiplexerStarted(role));
  FXL_DCHECK(IsUserDLCI(dlci));
  return (role == Role::kInitiator ? 1 : 0) == dlci % 2;
}

}  // namespace

void Session::SendUserData(DLCI dlci, common::ByteBufferPtr data) {
  bool sent = SendFrame(std::make_unique<UserDataFrame>(
      role_, credit_based_flow_, dlci, std::move(data)));
  FXL_DCHECK(sent);
}

std::unique_ptr<Session> Session::Create(
    fbl::RefPtr<l2cap::Channel> l2cap_channel,
    ChannelOpenedCallback channel_opened_cb) {
  auto session =
      std::unique_ptr<Session>(new Session(std::move(channel_opened_cb)));
  if (!session->SetL2CAPChannel(l2cap_channel))
    return nullptr;
  return session;
}

Session::Session(ChannelOpenedCallback channel_opened_cb)
    : role_(Role::kUnassigned),
      channel_opened_cb_(std::move(channel_opened_cb)),
      dispatcher_(async_get_default_dispatcher()),
      initial_param_negotiation_state_(
          ParameterNegotiationState::kNotNegotiated),
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
    if (role_ == Role::kUnassigned)
      StartupMultiplexer();
    return;
  }

  // RFCOMM 5.4: An RFCOMM entity making a new DLC on an existing session forms
  // the DLCI by combining the Server Channel for the application on the other
  // device, and the inverse of its own direction bit for the session."
  DLCI dlci = ServerChannelToDLCI(server_channel, OppositeRole(role_));

  if (initial_param_negotiation_state_ !=
      ParameterNegotiationState::kNegotiated) {
    tasks_pending_parameter_negotiation_.emplace(
        [this, server_channel, cb = std::move(channel_opened_cb)]() mutable {
          OpenRemoteChannel(server_channel, std::move(cb));
        });

    // If it's not started, start it
    if (initial_param_negotiation_state_ ==
        ParameterNegotiationState::kNotNegotiated)
      RunInitialParameterNegotiation(dlci);

    return;
  }

  SendCommand(
      FrameType::kSetAsynchronousBalancedMode, dlci,
      [this, dlci, server_channel, cb = std::move(channel_opened_cb)](
          std::unique_ptr<Frame> response) mutable {
        FXL_DCHECK(response);
        FrameType type = FrameType(response->control());
        fbl::RefPtr<Channel> new_channel;
        switch (type) {
          case FrameType::kUnnumberedAcknowledgement: {
            FXL_LOG(INFO) << "rfcomm: Channel " << static_cast<unsigned>(dlci)
                          << " started successfully";
            new_channel = fbl::AdoptRef(new internal::ChannelImpl(dlci, this));
            FXL_DCHECK(channels_.find(dlci) == channels_.end());
            channels_.emplace(dlci, new_channel);
            break;
          }
          case FrameType::kDisconnectedMode:
            FXL_LOG(WARNING)
                << "rfcomm: Channel " << static_cast<unsigned>(dlci)
                << " failed to start";
            break;
          default:
            FXL_LOG(WARNING) << "rfcomm: Unexpected response to SABM: "
                             << static_cast<unsigned>(type);
            break;
        }

        // Send the result.
        async::PostTask(dispatcher_,
                        [server_channel, new_channel, cb_ = std::move(cb)] {
                          cb_(new_channel, server_channel);
                        });
      });
}
void Session::RxCallback(const l2cap::SDU& sdu) {
  l2cap::PDU::Reader reader(&sdu);
  reader.ReadNext(sdu.length(), [&](const common::ByteBuffer& buffer) {
    auto frame = Frame::Parse(credit_based_flow_, OppositeRole(role_), buffer);
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

      case FrameType::kUnnumberedInfoHeaderCheck: {
        if (dlci == kMuxControlDLCI) {
          HandleMuxCommand(
              static_cast<MuxCommandFrame*>(frame.get())->TakeMuxCommand());
          return;
        } else if (IsUserDLCI(dlci)) {
          auto channel_it = channels_.find(dlci);
          if (channel_it == channels_.end()) {
            FXL_LOG(WARNING) << "rfcomm: User data received for unopened DLCI "
                             << static_cast<unsigned>(dlci);
            return;
          }
          channel_it->second->Receive(
              static_cast<UserDataFrame*>(frame.get())->TakeInformation());
          return;
        } else {
          FXL_LOG(WARNING) << "rfcomm: UIH frame on invalid DLCI "
                           << static_cast<unsigned>(dlci);
        }
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

void Session::SendCommand(FrameType frame_type, DLCI dlci,
                          CommandResponseCallback command_response_cb) {
  FXL_DCHECK(frame_type == FrameType::kSetAsynchronousBalancedMode ||
             frame_type == FrameType::kDisconnect);
  FXL_DCHECK(IsValidDLCI(dlci));
  FXL_DCHECK(outstanding_frames_.find(dlci) == outstanding_frames_.end())
      << "rfcomm: There is already an outstanding command frame for DLCI "
      << static_cast<unsigned>(dlci);

  auto timeout_cb = std::make_unique<async::TaskClosure>([this, dlci] {
    FXL_LOG(ERROR) << "rfcomm: Outstanding frame on DLCI "
                   << static_cast<unsigned>(dlci)
                   << " timed out; closing down session";
    Closedown();
  });

  // Set response and timeout callbacks.
  auto callbacks =
      std::make_pair(std::move(command_response_cb), std::move(timeout_cb));
  outstanding_frames_.emplace(dlci, std::move(callbacks));

  // A different timeout is used if this is a SABM command on a user data
  // channel (RFCOMM 5.3).
  zx::duration timeout =
      (frame_type == FrameType::kSetAsynchronousBalancedMode &&
       IsUserDLCI(dlci))
          ? kAcknowledgementTimerUserDLCs
          : kAcknowledgementTimer;

  std::unique_ptr<Frame> frame;
  if (frame_type == FrameType::kSetAsynchronousBalancedMode) {
    frame = std::make_unique<SetAsynchronousBalancedModeCommand>(role_, dlci);
  } else {
    frame = std::make_unique<DisconnectCommand>(role_, dlci);
  }

  bool sent = SendFrame(std::move(frame), [this, timeout, dlci] {
    outstanding_frames_[dlci].second->PostDelayed(dispatcher_, timeout);
  });
  FXL_DCHECK(sent);
}

void Session::SendResponse(FrameType frame_type, DLCI dlci) {
  FXL_DCHECK(frame_type == FrameType::kUnnumberedAcknowledgement ||
             frame_type == FrameType::kDisconnectedMode);
  FXL_DCHECK(IsValidDLCI(dlci));

  std::unique_ptr<Frame> frame;
  if (frame_type == FrameType::kUnnumberedAcknowledgement)
    frame = std::make_unique<UnnumberedAcknowledgementResponse>(role_, dlci);
  else
    frame = std::make_unique<DisconnectedModeResponse>(role_, dlci);

  bool sent = SendFrame(std::move(frame));
  FXL_DCHECK(sent);
}

bool Session::SendFrame(std::unique_ptr<Frame> frame, fit::closure sent_cb) {
  FXL_DCHECK(frame);
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

  if (l2cap_channel_->Send(std::move(buffer))) {
    if (sent_cb)
      sent_cb();
    return true;
  } else {
    FXL_LOG(ERROR) << "rfcomm: Failed to send frame";
    return false;
  }
}

void Session::SendMuxCommand(std::unique_ptr<MuxCommand> mux_command,
                             MuxResponseCallback callback) {
  FXL_DCHECK(mux_command);

  // If we're not expecting a response, we can send right away.
  if (!callback) {
    auto frame = std::make_unique<MuxCommandFrame>(role_, credit_based_flow_,
                                                   std::move(mux_command));
    bool sent = SendFrame(std::move(frame));
    FXL_DCHECK(sent);
    return;
  }

  // If we're expecting a response at the mulitplexer level, store the callback
  // that should be called when the response arrives.
  MuxCommandType type = mux_command->command_type();
  DLCI dlci = GetDLCIFromMuxCommand(mux_command.get());
  OutstandingMuxCommand key = std::make_pair(type, dlci);

  FXL_DCHECK(outstanding_mux_commands_.find(key) ==
             outstanding_mux_commands_.end())
      << "rfcomm: Already an outstanding mux command for (command type,"
      << " dlci) = (" << static_cast<unsigned>(type) << ", "
      << static_cast<unsigned>(dlci) << ")";

  auto timeout_cb =
      std::make_unique<async::TaskClosure>([this] { Closedown(); });

  // Set response and timeout callbacks.
  outstanding_mux_commands_.emplace(
      key, std::make_pair(std::move(callback), std::move(timeout_cb)));

  auto frame = std::make_unique<MuxCommandFrame>(role_, credit_based_flow_,
                                                 std::move(mux_command));

  bool sent = SendFrame(std::move(frame), [this, key] {
    outstanding_mux_commands_[key].second->PostDelayed(dispatcher_,
                                                       kMuxResponseTimer);
  });
  FXL_DCHECK(sent);
}

void Session::StartupMultiplexer() {
  if (role_ == Role::kNegotiating || multiplexer_started()) {
    FXL_LOG(WARNING) << "rfcomm: StartupMultiplexer when starting or started";
    return;
  }

  FXL_LOG(INFO) << "rfcomm: Starting multiplexer";

  role_ = Role::kNegotiating;

  SendCommand(FrameType::kSetAsynchronousBalancedMode, kMuxControlDLCI,
              [this](auto response) {
                FXL_DCHECK(response);

                FrameType type = static_cast<FrameType>(response->control());
                FXL_DCHECK(type == FrameType::kUnnumberedAcknowledgement ||
                           type == FrameType::kDisconnectedMode);

                switch (role_) {
                  case Role::kNegotiating: {
                    if (type == FrameType::kUnnumberedAcknowledgement) {
                      SetMultiplexerStarted(Role::kInitiator);
                    } else {
                      FXL_LOG(WARNING)
                          << "rfcomm: Remote multiplexer startup refused"
                          << " by remote";
                      role_ = Role::kUnassigned;
                    }
                    return;
                  }
                  case Role::kUnassigned:
                  case Role::kInitiator:
                  case Role::kResponder:
                    // TODO(guss): should a UA be received in any of these
                    // cases?
                    FXL_LOG(WARNING)
                        << "rfcomm: Mux UA frame received in unexpected"
                        << " state";
                    break;
                  default:
                    // TODO(gusss): shouldn't get here.
                    FXL_NOTREACHED();
                    break;
                }
              });
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
        SendResponse(FrameType::kUnnumberedAcknowledgement, dlci);
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

        SendResponse(FrameType::kDisconnectedMode, kMuxControlDLCI);
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
        FXL_LOG(WARNING) << "rfcomm: Request to start already started"
                         << " multiplexer";
        return;
      default:
        FXL_NOTREACHED();
        return;
    }
  }

  // If it isn't a multiplexer startup request, it must be a request for a user
  // channel.

  // TODO(NET-1301): unit test this case.
  if (!IsUserDLCI(dlci) || !IsValidLocalChannel(role_, dlci)) {
    FXL_LOG(WARNING) << "rfcomm: Remote requested invalid DLCI "
                     << static_cast<unsigned>(dlci);
    SendResponse(FrameType::kDisconnectedMode, dlci);
    return;
  }

  // TODO(NET-1301): unit test this case.
  if (channels_.find(dlci) != channels_.end()) {
    // If the channel is already open, the remote is confused about the state of
    // the Session. Send a DM and a DISC for that channel.
    // TODO(NET-1274): do we want to just shut down the whole session here?
    // Things would be in a nasty state at this point.
    FXL_LOG(WARNING) << "rfcomm: Remote requested already open channel";
    SendResponse(FrameType::kDisconnectedMode, dlci);
    SendCommand(FrameType::kDisconnect, dlci, [](auto response) {
      // TODO(NET-1273): implement clean channel close + state reset
    });

    return;
  }

  // Start the channel by first responding positively.
  SendResponse(FrameType::kUnnumberedAcknowledgement, dlci);

  // Now form the channel and pass it off.
  auto channel = fbl::AdoptRef(new internal::ChannelImpl(dlci, this));
  channels_.emplace(dlci, channel);
  async::PostTask(dispatcher_, [this, dlci, channel] {
    channel_opened_cb_(channel, DLCIToServerChannel(dlci));
  });

  FXL_LOG(INFO) << "rfcomm: Remote peer opened channel with DLCI "
                << static_cast<unsigned>(dlci);
}

void Session::HandleMuxCommand(std::unique_ptr<MuxCommand> mux_command) {
  FXL_DCHECK(mux_command);
  MuxCommandType type = mux_command->command_type();

  if (mux_command->command_response() == CommandResponse::kResponse) {
    auto dlci = GetDLCIFromMuxCommand(mux_command.get());
    auto key = std::make_pair(type, dlci);

    auto command_it = outstanding_mux_commands_.find(key);
    if (command_it == outstanding_mux_commands_.end()) {
      FXL_LOG(WARNING)
          << "Got response, but no outstanding command for (type, DLCI) = "
          << "(" << unsigned(type) << ", " << unsigned(dlci) << ")";
      return;
    }

    // Cancel the timeout and call the callback.
    command_it->second.second->Cancel();
    async::PostTask(
        dispatcher_,
        [cmd = std::move(mux_command),
         cb = std::move(outstanding_mux_commands_[key].first)]() mutable {
          cb(std::move(cmd));
        });

    outstanding_mux_commands_.erase(command_it);

    return;
  }

  // Otherwise, it's a command.
  switch (type) {
    case MuxCommandType::kDLCParameterNegotiation: {
      auto pn_command = std::unique_ptr<DLCParameterNegotiationCommand>(
          static_cast<DLCParameterNegotiationCommand*>(mux_command.release()));
      ParameterNegotiationParams received_params = pn_command->params();

      if (!IsUserDLCI(received_params.dlci)) {
        FXL_LOG(WARNING)
            << "rfcomm: Received parameter negotiation command for invalid"
            << " DLCI " << static_cast<unsigned>(received_params.dlci);
        SendResponse(FrameType::kDisconnectedMode, received_params.dlci);
        return;
      }

      if (channels_negotiating_.find(received_params.dlci) !=
              channels_negotiating_.end() &&
          channels_negotiating_[received_params.dlci] !=
              ParameterNegotiationState::kNotNegotiated) {
        // RFCOMM 5.5.3 states that supporting re-negotiation of DLCIs is
        // optional, and that instead we may just reply with our own parameters.
        FXL_LOG(WARNING)
            << "rfcomm: Request to negotiate already-negotiated DLCI";
        SendResponse(FrameType::kDisconnectedMode, received_params.dlci);
        ParameterNegotiationParams our_params =
            GetIdealParameters(received_params.dlci);
        our_params.credit_based_flow_handshake =
            CreditBasedFlowHandshake::kSupportedResponse;
        our_params.maximum_frame_size = maximum_frame_size_;
        SendMuxCommand(std::make_unique<DLCParameterNegotiationCommand>(
            CommandResponse::kResponse, our_params));
        return;
      }

      if (initial_param_negotiation_state_ ==
              ParameterNegotiationState::kNegotiated &&
          received_params.maximum_frame_size != maximum_frame_size_) {
        // RFCOMM 5.5.3 states that a responder may issue a DM frame if they are
        // unwilling to establish a connection. In this case, we use it to
        // reject any non-initial PN command which attempts to change the
        // maximum frame size.
        FXL_LOG(WARNING) << "rfcomm: Peer requested different max frame size"
                         << " after initial negotiation complete; rejecting";
        SendResponse(FrameType::kDisconnectedMode, received_params.dlci);
        return;
      }

      ParameterNegotiationParams ideal_params =
          GetIdealParameters(received_params.dlci);

      // Parameter negotiation described in GSM 5.4.6.3.1 (under table 5).
      ParameterNegotiationParams negotiated_params;
      // DLCI does not change.
      negotiated_params.dlci = received_params.dlci;
      // Respond with a positive credit-based flow handshake response iff we
      // received a positive request.
      negotiated_params.credit_based_flow_handshake =
          received_params.credit_based_flow_handshake ==
                  CreditBasedFlowHandshake::kSupportedRequest
              ? CreditBasedFlowHandshake::kSupportedResponse
              : CreditBasedFlowHandshake::kUnsupported;
      // Priority does not change.
      negotiated_params.priority = received_params.priority;

      // Accept their max frame size if this is the initial negotiation and if
      // it's less than or equal to ours; otherwise, use ours.
      if (initial_param_negotiation_state_ !=
          ParameterNegotiationState::kNegotiated) {
        negotiated_params.maximum_frame_size =
            received_params.maximum_frame_size <=
                    ideal_params.maximum_frame_size
                ? received_params.maximum_frame_size
                : ideal_params.maximum_frame_size;
      } else {
        FXL_DCHECK(received_params.maximum_frame_size == maximum_frame_size_);
        negotiated_params.maximum_frame_size = maximum_frame_size_;
      }

      negotiated_params.initial_credits = ideal_params.initial_credits;

      // Update settings.

      if (initial_param_negotiation_state_ !=
          ParameterNegotiationState::kNegotiated) {
        // Set credit-based flow and max frame size only on initial PN.
        credit_based_flow_ = received_params.credit_based_flow_handshake ==
                             CreditBasedFlowHandshake::kSupportedRequest;
        maximum_frame_size_ = negotiated_params.maximum_frame_size;
        InitialParameterNegotiationComplete();
      }

      // TODO(NET-1130): set priority if/when priority is implemented.

      // TODO(NET-1079): receive credits when credit-based flow is implemented.

      FXL_LOG(INFO) << "rfcomm: Parameters negotiated:"
                    << " DLCI " << unsigned(negotiated_params.dlci) << ","
                    << " Credit-based flow "
                    << (negotiated_params.credit_based_flow_handshake ==
                                CreditBasedFlowHandshake::kSupportedResponse
                            ? "on,"
                            : "off,")
                    << " (credits "
                    << static_cast<unsigned>(negotiated_params.initial_credits)
                    << "),"
                    << " Priority "
                    << static_cast<unsigned>(negotiated_params.priority) << ","
                    << " Max frame size "
                    << negotiated_params.maximum_frame_size;

      // Respond with the negotiated params.
      SendMuxCommand(std::make_unique<DLCParameterNegotiationCommand>(
          CommandResponse::kResponse, negotiated_params));

      channels_negotiating_[negotiated_params.dlci] =
          ParameterNegotiationState::kNegotiated;

      return;
    }
    default: {
      FXL_NOTIMPLEMENTED();
      break;
    }
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

void Session::RunInitialParameterNegotiation(DLCI dlci) {
  FXL_DCHECK(multiplexer_started())
      << "Parameter negotiation requested before multiplexer started";
  FXL_DCHECK(initial_param_negotiation_state_ ==
             ParameterNegotiationState::kNotNegotiated)
      << "Initial parameter negotiation already run";

  // Mark the DLCI as in the negotiating state.
  channels_negotiating_[dlci] = ParameterNegotiationState::kNegotiating;
  initial_param_negotiation_state_ = ParameterNegotiationState::kNegotiating;

  auto params = GetIdealParameters(dlci);

  auto pn_command = std::make_unique<DLCParameterNegotiationCommand>(
      CommandResponse::kCommand, params);
  SendMuxCommand(std::move(pn_command), [this, dlci, priority = params.priority,
                                         maximum_frame_size =
                                             params.maximum_frame_size](
                                            auto mux_command) {
    FXL_DCHECK(initial_param_negotiation_state_ ==
                   ParameterNegotiationState::kNegotiating ||
               initial_param_negotiation_state_ ==
                   ParameterNegotiationState::kNegotiated);

    if (mux_command == nullptr) {
      // A response of nullptr signals a DM response from the peer.
      FXL_LOG(INFO) << "rfcomm: PN command for DLCI "
                    << static_cast<unsigned>(dlci) << " rejected";
      channels_negotiating_[dlci] = ParameterNegotiationState::kNotNegotiated;

      // If initial parameter negotiation didn't already finish since we started
      // this PN command, then reset it back to kNotNegotiated.
      if (initial_param_negotiation_state_ ==
          ParameterNegotiationState::kNegotiating)
        initial_param_negotiation_state_ =
            ParameterNegotiationState::kNotNegotiated;
      return;
    }

    FXL_DCHECK(mux_command->command_type() ==
                   MuxCommandType::kDLCParameterNegotiation &&
               mux_command->command_response() == CommandResponse::kResponse);

    auto pn_response = std::unique_ptr<DLCParameterNegotiationCommand>(
        static_cast<DLCParameterNegotiationCommand*>(mux_command.release()));
    auto params = pn_response->params();

    if (dlci != params.dlci) {
      FXL_LOG(WARNING) << "rfcomm: Remote changed DLCI in PN response";
      SendCommand(FrameType::kDisconnect, dlci);
      channels_negotiating_[dlci] = ParameterNegotiationState::kNotNegotiated;
      if (initial_param_negotiation_state_ ==
          ParameterNegotiationState::kNegotiating)
        initial_param_negotiation_state_ =
            ParameterNegotiationState::kNotNegotiated;
      return;
    }

    // TODO(gusss): currently we completely ignore priority (other than this
    // check)
    if (params.priority != priority)
      FXL_LOG(WARNING) << "rfcomm: Remote changed priority in PN response";

    if (params.maximum_frame_size > maximum_frame_size) {
      FXL_LOG(WARNING)
          << "rfcomm: Peer's PN response contained an invalid max frame size";
      SendCommand(FrameType::kDisconnect, dlci);
      channels_negotiating_[dlci] = ParameterNegotiationState::kNotNegotiated;
      if (initial_param_negotiation_state_ ==
          ParameterNegotiationState::kNegotiating)
        initial_param_negotiation_state_ =
            ParameterNegotiationState::kNotNegotiated;
      return;
    }

    if (initial_param_negotiation_state_ ==
            ParameterNegotiationState::kNegotiated &&
        params.maximum_frame_size != maximum_frame_size_) {
      FXL_LOG(WARNING)
          << "rfcomm: Peer tried to change max frame size after initial param"
          << " negotiation completed; rejecting";
      SendCommand(FrameType::kDisconnect, dlci);
      channels_negotiating_[dlci] = ParameterNegotiationState::kNotNegotiated;
      if (initial_param_negotiation_state_ ==
          ParameterNegotiationState::kNegotiating)
        initial_param_negotiation_state_ =
            ParameterNegotiationState::kNotNegotiated;
      return;
    }

    // Only set these parameters on initial parameter negotiation.
    if (initial_param_negotiation_state_ ==
        ParameterNegotiationState::kNegotiating) {
      // Credit-based flow is turned on if the peer sends the correct
      // response.
      credit_based_flow_ = params.credit_based_flow_handshake ==
                           CreditBasedFlowHandshake::kSupportedResponse;

      maximum_frame_size_ = params.maximum_frame_size;
      InitialParameterNegotiationComplete();
    }

    // TODO(NET-1079): Handle credits here when credit-based flow implemented
    // HandleReceivedCredits(dlci, params.initial_credits);

    FXL_LOG(INFO) << "rfcomm: Parameters negotiated:"
                  << " DLCI " << unsigned(params.dlci) << ","
                  << " Credit-based flow "
                  << (credit_based_flow_ ? "on" : "off") << " (credits "
                  << static_cast<unsigned>(params.initial_credits) << "),"
                  << " Priority " << static_cast<unsigned>(params.priority)
                  << ","
                  << " Max frame size " << maximum_frame_size_;

    // Set channel to not negotiating anymore.
    FXL_DCHECK(channels_negotiating_.find(dlci) != channels_negotiating_.end());
    channels_negotiating_[dlci] = ParameterNegotiationState::kNegotiated;
  });
}

ParameterNegotiationParams Session::GetIdealParameters(DLCI dlci) const {
  FXL_DCHECK(IsValidDLCI(dlci));

  // We set the MTU of the RFCOMM channel based on the MTUs of the underlying
  // L2CAP link; we take the minimum of the two.
  uint16_t maximum_frame_size =
      (l2cap_channel_->rx_mtu() < l2cap_channel_->tx_mtu()
           ? l2cap_channel_->rx_mtu()
           : l2cap_channel_->tx_mtu());

  // GSM Table 27.
  Priority priority = 61;
  if (dlci == kMuxControlDLCI) {
    priority = 0;
  } else if (dlci <= 7) {
    priority = 7;
  } else if (dlci <= 15) {
    priority = 15;
  } else if (dlci <= 23) {
    priority = 23;
  } else if (dlci <= 31) {
    priority = 31;
  } else if (dlci <= 39) {
    priority = 39;
  } else if (dlci <= 47) {
    priority = 47;
  } else if (dlci <= 55) {
    priority = 55;
  }

  return {dlci,
          // We always attempt to enable credit-based flow (RFCOMM 5.5.3).
          CreditBasedFlowHandshake::kSupportedRequest,
          // TODO(NET-1079): send initial credits when credit-based flow
          // implemented.
          priority, maximum_frame_size, 0};
}

void Session::InitialParameterNegotiationComplete() {
  FXL_LOG(INFO) << "rfcomm: Initial parameter negotiation complete";

  initial_param_negotiation_state_ = ParameterNegotiationState::kNegotiated;

  while (!tasks_pending_parameter_negotiation_.empty()) {
    async::PostTask(dispatcher_,
                    std::move(tasks_pending_parameter_negotiation_.front()));
    tasks_pending_parameter_negotiation_.pop();
  }

  // TODO(gusss): send frames from queue when queueing is implemented.
}

}  // namespace rfcomm
}  // namespace btlib
