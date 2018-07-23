// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/common/slab_allocator.h"
#include "garnet/drivers/bluetooth/lib/l2cap/fake_channel_test.h"
#include "garnet/drivers/bluetooth/lib/l2cap/fake_layer.h"
#include "garnet/drivers/bluetooth/lib/rfcomm/channel_manager.h"

namespace btlib {
namespace rfcomm {
namespace {

constexpr l2cap::ChannelId kL2CAPChannelId1 = 0x0040;
constexpr l2cap::ChannelId kL2CAPChannelId2 = 0x0041;
constexpr hci::ConnectionHandle kHandle1 = 1;

void DoNothingWithChannel(fbl::RefPtr<Channel> channel,
                          ServerChannel server_channel) {}

class RFCOMM_ChannelManagerTest : public l2cap::testing::FakeChannelTest {
 public:
  RFCOMM_ChannelManagerTest() : channel_manager_(nullptr) {}
  ~RFCOMM_ChannelManagerTest() override = default;

 protected:
  // Captures the state of the fake remote peer.
  struct PeerState {
    // Whether this peer supports credit-based flow. This bool also indicates
    // whether the session with this peer will have credit-based flow turned on;
    // our RFCOMM implementation will always turn on credit-based flow if the
    // peer supports it.
    bool credit_based_flow;
    Role role;
  };

  void SetUp() override {
    l2cap_ = l2cap::testing::FakeLayer::Create();
    FXL_DCHECK(l2cap_);

    l2cap_->Initialize();
    l2cap_->AddACLConnection(kHandle1, hci::Connection::Role::kMaster,
                             [] { FXL_LOG(WARNING) << "Unimplemented"; },
                             dispatcher());
    // Any new L2CAP channels (incoming our outgoing) opened by our
    // ChannelManager will be captured and stored in |handle_to_fake_channel_|.
    // Subsequently, all channels will have listeners attached to them, and any
    // frames sent from our RFCOMM sessions will be put into the queues in
    // |handle_to_incoming_frames_|.
    l2cap_->set_channel_callback(
        [this](fbl::RefPtr<l2cap::testing::FakeChannel> l2cap_channel) {
          FXL_DCHECK(l2cap_channel);
          auto handle = l2cap_channel->link_handle();
          handle_to_fake_channel_.emplace(handle, l2cap_channel);
          l2cap_channel->SetSendCallback(
              [this, handle](auto sdu) {
                if (handle_to_incoming_frames_.find(handle) ==
                    handle_to_incoming_frames_.end()) {
                  handle_to_incoming_frames_.emplace(
                      handle,
                      std::queue<std::unique_ptr<const common::ByteBuffer>>());
                }
                handle_to_incoming_frames_[handle].push(std::move(sdu));
              },
              dispatcher());
        });

    channel_manager_ = ChannelManager::Create(l2cap_.get());
    FXL_DCHECK(channel_manager_);
  }

  void TearDown() override {
    channel_manager_.release();
    l2cap_.reset();
    handle_to_peer_state_.clear();
    handle_to_fake_channel_.clear();
    handle_to_incoming_frames_.clear();
  }

  void ExpectFrame(hci::ConnectionHandle handle, FrameType type, DLCI dlci) {
    auto queue_it = handle_to_incoming_frames_.find(handle);
    EXPECT_FALSE(handle_to_incoming_frames_.end() == queue_it);

    EXPECT_FALSE(handle_to_peer_state_.find(handle) ==
                 handle_to_peer_state_.end());
    const PeerState& state = handle_to_peer_state_[handle];

    auto frame = Frame::Parse(state.credit_based_flow, OppositeRole(state.role),
                              queue_it->second.front()->view());
    queue_it->second.pop();
    EXPECT_TRUE(frame);
    EXPECT_EQ(type, static_cast<FrameType>(frame->control()));
    EXPECT_EQ(dlci, frame->dlci());
  }

  void ReceiveFrame(hci::ConnectionHandle handle,
                    std::unique_ptr<Frame> frame) {
    auto channel_it = handle_to_fake_channel_.find(handle);
    EXPECT_FALSE(channel_it == handle_to_fake_channel_.end());

    auto buffer = common::NewSlabBuffer(frame->written_size());
    frame->Write(buffer->mutable_view());
    channel_it->second->Receive(buffer->view());
  }

  // Emplace a new PeerState for a new fake peer. Should be called for each fake
  // peer which a test is emulating. The returned PeerState should then be
  // updated throughout the test (e.g. the multiplexer state should change when
  // the multiplexer starts up).
  PeerState& AddFakePeerState(hci::ConnectionHandle handle, PeerState state) {
    FXL_DCHECK(handle_to_peer_state_.find(handle) ==
               handle_to_peer_state_.end());
    handle_to_peer_state_.emplace(handle, std::move(state));
    return handle_to_peer_state_[handle];
  }

  fbl::RefPtr<l2cap::testing::FakeChannel> GetFakeChannel(
      hci::ConnectionHandle handle) {
    FXL_DCHECK(handle_to_fake_channel_.find(handle) !=
               handle_to_fake_channel_.end());
    return handle_to_fake_channel_[handle];
  }

  std::unique_ptr<ChannelManager> channel_manager_;

  fbl::RefPtr<l2cap::testing::FakeLayer> l2cap_;

  std::unordered_map<hci::ConnectionHandle,
                     std::queue<std::unique_ptr<const common::ByteBuffer>>>
      handle_to_incoming_frames_;

  // Maps remote peers (represented as connection handles) to the L2CAP channel
  // (actually a FakeChannel) over which the corresponding RFCOMM session
  // communicates with that peer.
  std::unordered_map<hci::ConnectionHandle,
                     fbl::RefPtr<l2cap::testing::FakeChannel>>
      handle_to_fake_channel_;

  // Holds the state of the fake peers. Tests must manually update this
  // information as needed; for example, if a test mimics mux startup manually,
  // it must change its role accordingly, otherwise utility functions like
  // ExpectFrame() will not parse frames correctly.
  std::unordered_map<hci::ConnectionHandle, PeerState> handle_to_peer_state_;
};

// Expect that registration of an L2CAP channel with the Channel Manager results
// in the L2CAP channel's eventual activation.
TEST_F(RFCOMM_ChannelManagerTest, RegisterL2CAPChannel) {
  ChannelOptions l2cap_channel_options(kL2CAPChannelId1);
  auto l2cap_channel = CreateFakeChannel(l2cap_channel_options);
  EXPECT_TRUE(channel_manager_->RegisterL2CAPChannel(l2cap_channel));
  EXPECT_TRUE(l2cap_channel->activated());
}

// Test that command timeouts during multiplexer startup result in the session
// being closed down.
TEST_F(RFCOMM_ChannelManagerTest, MuxStartupAndParamNegotiation_Timeout) {
  AddFakePeerState(kHandle1, PeerState{true /*credits*/, Role::kUnassigned});

  channel_manager_->OpenRemoteChannel(kHandle1, kMinServerChannel,
                                      &DoNothingWithChannel, dispatcher());
  l2cap_->TriggerOutboundChannel(kHandle1, l2cap::kRFCOMM, kL2CAPChannelId1,
                                 kL2CAPChannelId2);
  RunLoopUntilIdle();

  auto channel = GetFakeChannel(kHandle1);

  ExpectFrame(kHandle1, FrameType::kSetAsynchronousBalancedMode,
              kMuxControlDLCI);

  // Do nothing
  RunLoopFor(zx::min(5));

  // Expect closedown after timeout
  EXPECT_FALSE(channel->activated());
}

// Test successful multiplexer startup (resulting role: responder).
TEST_F(RFCOMM_ChannelManagerTest, MuxStartupAndParamNegotiation_Responder) {
  AddFakePeerState(kHandle1, PeerState{true /*credits*/, Role::kUnassigned});

  l2cap_->TriggerInboundChannel(kHandle1, l2cap::kRFCOMM, kL2CAPChannelId1,
                                kL2CAPChannelId2);
  RunLoopUntilIdle();

  // Receive a multiplexer startup frame on the session
  ReceiveFrame(kHandle1, std::make_unique<SetAsynchronousBalancedModeCommand>(
                             Role::kUnassigned, kMuxControlDLCI));
  RunLoopUntilIdle();

  ExpectFrame(kHandle1, FrameType::kUnnumberedAcknowledgement, kMuxControlDLCI);
}

// Test successful multiplexer startup (resulting role: initiator)
TEST_F(RFCOMM_ChannelManagerTest, MuxStartupAndParamNegotiation_Initiator) {
  auto& state = AddFakePeerState(
      kHandle1, PeerState{true /*credits*/, Role::kUnassigned});

  bool channel_received = false;
  fbl::RefPtr<Channel> channel;
  channel_manager_->OpenRemoteChannel(
      kHandle1, kMinServerChannel,
      [&channel, &channel_received](auto ch, auto server_channel) {
        channel_received = true;
        channel = ch;
      },
      dispatcher());
  l2cap_->TriggerOutboundChannel(kHandle1, l2cap::kRFCOMM, kL2CAPChannelId1,
                                 kL2CAPChannelId2);
  RunLoopUntilIdle();

  ExpectFrame(kHandle1, FrameType::kSetAsynchronousBalancedMode,
              kMuxControlDLCI);

  // Receive a UA on the session
  ReceiveFrame(kHandle1, std::make_unique<UnnumberedAcknowledgementResponse>(
                             Role::kUnassigned, kMuxControlDLCI));
  RunLoopUntilIdle();

  state.role = Role::kResponder;

  {
    // Expect a PN command from the session
    auto queue_it = handle_to_incoming_frames_.find(kHandle1);
    EXPECT_FALSE(queue_it == handle_to_incoming_frames_.end());
    EXPECT_EQ(1ul, queue_it->second.size());
    auto frame = Frame::Parse(true, OppositeRole(state.role),
                              queue_it->second.front()->view());
    queue_it->second.pop();
    EXPECT_EQ(FrameType::kUnnumberedInfoHeaderCheck,
              static_cast<FrameType>(frame->control()));
    DLCI dlci =
        ServerChannelToDLCI(kMinServerChannel, OppositeRole(state.role));
    auto mux_command =
        static_cast<MuxCommandFrame*>(frame.get())->TakeMuxCommand();
    EXPECT_EQ(MuxCommandType::kDLCParameterNegotiation,
              mux_command->command_type());

    auto params =
        static_cast<DLCParameterNegotiationCommand*>(mux_command.get())
            ->params();
    EXPECT_EQ(dlci, params.dlci);
    params.credit_based_flow_handshake =
        CreditBasedFlowHandshake::kSupportedResponse;

    // Receive PN response
    ReceiveFrame(kHandle1, std::make_unique<MuxCommandFrame>(
                               state.role, true,
                               std::make_unique<DLCParameterNegotiationCommand>(
                                   CommandResponse::kResponse, params)));
    RunLoopUntilIdle();
  }

  EXPECT_TRUE(channel_received);
  EXPECT_FALSE(channel);
}

// Test multiplexer startup conflict procedure (resulting role: initiator).
TEST_F(RFCOMM_ChannelManagerTest,
       MuxStartupAndParamNegotiation_Conflict_BecomeInitiator) {
  auto& state = AddFakePeerState(
      kHandle1, PeerState{true /*credits*/, Role::kUnassigned});

  bool channel_received = false;
  fbl::RefPtr<Channel> channel;
  channel_manager_->OpenRemoteChannel(
      kHandle1, kMinServerChannel,
      [&channel, &channel_received](auto ch, auto server_channel) {
        channel_received = true;
        channel = ch;
      },
      dispatcher());
  l2cap_->TriggerOutboundChannel(kHandle1, l2cap::kRFCOMM, kL2CAPChannelId1,
                                 kL2CAPChannelId2);
  RunLoopUntilIdle();

  ExpectFrame(kHandle1, FrameType::kSetAsynchronousBalancedMode,
              kMuxControlDLCI);

  // Receive a conflicting SABM on the session
  ReceiveFrame(kHandle1, std::make_unique<SetAsynchronousBalancedModeCommand>(
                             state.role, kMuxControlDLCI));
  RunLoopUntilIdle();

  ExpectFrame(kHandle1, FrameType::kDisconnectedMode, kMuxControlDLCI);

  // Wait and expect a SABM
  RunLoopFor(zx::sec(5));
  ExpectFrame(kHandle1, FrameType::kSetAsynchronousBalancedMode,
              kMuxControlDLCI);

  // Receive a UA on the session
  ReceiveFrame(kHandle1, std::make_unique<UnnumberedAcknowledgementResponse>(
                             state.role, kMuxControlDLCI));
  RunLoopUntilIdle();

  state.role = Role::kResponder;

  {
    // Expect a PN command from the session
    auto queue_it = handle_to_incoming_frames_.find(kHandle1);
    EXPECT_FALSE(queue_it == handle_to_incoming_frames_.end());
    EXPECT_EQ(1ul, queue_it->second.size());
    auto frame = Frame::Parse(true, OppositeRole(state.role),
                              queue_it->second.front()->view());
    queue_it->second.pop();
    EXPECT_EQ(FrameType::kUnnumberedInfoHeaderCheck,
              static_cast<FrameType>(frame->control()));
    DLCI dlci =
        ServerChannelToDLCI(kMinServerChannel, OppositeRole(state.role));
    auto mux_command =
        static_cast<MuxCommandFrame*>(frame.get())->TakeMuxCommand();
    EXPECT_EQ(MuxCommandType::kDLCParameterNegotiation,
              mux_command->command_type());

    auto params =
        static_cast<DLCParameterNegotiationCommand*>(mux_command.get())
            ->params();
    EXPECT_EQ(dlci, params.dlci);
    params.credit_based_flow_handshake =
        CreditBasedFlowHandshake::kSupportedResponse;

    // Receive PN response
    ReceiveFrame(kHandle1, std::make_unique<MuxCommandFrame>(
                               state.role, true,
                               std::make_unique<DLCParameterNegotiationCommand>(
                                   CommandResponse::kResponse, params)));
    RunLoopUntilIdle();
  }

  EXPECT_TRUE(channel_received);
  EXPECT_FALSE(channel);
}

// Test multiplexer startup conflict procedure (resulting role: responder).
TEST_F(RFCOMM_ChannelManagerTest,
       MuxStartupAndParamNegotiation_Conflict_BecomeResponder) {
  auto& state = AddFakePeerState(
      kHandle1, PeerState{true /*credits*/, Role::kUnassigned});

  bool channel_delivered = false;
  channel_manager_->OpenRemoteChannel(
      kHandle1, kMinServerChannel,
      [&channel_delivered](auto channel, auto server_channel) {
        channel_delivered = true;
      },
      dispatcher());
  l2cap_->TriggerOutboundChannel(kHandle1, l2cap::kRFCOMM, kL2CAPChannelId1,
                                 kL2CAPChannelId2);
  RunLoopUntilIdle();

  // Expect initial mux-opening SABM
  ExpectFrame(kHandle1, FrameType::kSetAsynchronousBalancedMode,
              kMuxControlDLCI);

  // Receive a conflicting SABM on the session
  state.role = Role::kNegotiating;
  ReceiveFrame(kHandle1, std::make_unique<SetAsynchronousBalancedModeCommand>(
                             state.role, kMuxControlDLCI));
  RunLoopUntilIdle();

  // Expect a DM frame from the session
  ExpectFrame(kHandle1, FrameType::kDisconnectedMode, kMuxControlDLCI);

  // Immediately receive another SABM on the session
  ReceiveFrame(kHandle1, std::make_unique<SetAsynchronousBalancedModeCommand>(
                             state.role, kMuxControlDLCI));
  RunLoopUntilIdle();

  // Expect UA
  ExpectFrame(kHandle1, FrameType::kUnnumberedAcknowledgement, kMuxControlDLCI);
  state.role = Role::kInitiator;

  {
    // Expect a PN command from the session
    EXPECT_FALSE(handle_to_incoming_frames_.find(kHandle1) ==
                 handle_to_incoming_frames_.end());
    auto& queue = handle_to_incoming_frames_[kHandle1];
    EXPECT_EQ(1ul, queue.size());
    auto frame = Frame::Parse(state.credit_based_flow, OppositeRole(state.role),
                              queue.front()->view());
    queue.pop();
    EXPECT_EQ(FrameType::kUnnumberedInfoHeaderCheck,
              static_cast<FrameType>(frame->control()));
    DLCI dlci =
        ServerChannelToDLCI(kMinServerChannel, OppositeRole(state.role));
    auto mux_command =
        static_cast<MuxCommandFrame*>(frame.get())->TakeMuxCommand();
    EXPECT_EQ(MuxCommandType::kDLCParameterNegotiation,
              mux_command->command_type());

    auto params =
        static_cast<DLCParameterNegotiationCommand*>(mux_command.get())
            ->params();
    EXPECT_EQ(dlci, params.dlci);
    params.credit_based_flow_handshake =
        CreditBasedFlowHandshake::kSupportedResponse;

    // Receive PN response
    ReceiveFrame(kHandle1, std::make_unique<MuxCommandFrame>(
                               state.role, true,
                               std::make_unique<DLCParameterNegotiationCommand>(
                                   CommandResponse::kResponse, params)));
    RunLoopUntilIdle();
  }

  // EXPECT_TRUE(channel_received);
  // EXPECT_FALSE(channel);
}

// Tests whether sessions handle invalid max frame sizes correctly.
TEST_F(RFCOMM_ChannelManagerTest,
       MuxStartupAndParamNegotiation_BadPN_InvalidMaxFrameSize) {
  auto& state = AddFakePeerState(
      kHandle1, PeerState{true /*credits*/, Role::kUnassigned});

  bool channel_delivered = false;
  channel_manager_->OpenRemoteChannel(
      kHandle1, kMinServerChannel,
      [&channel_delivered](auto channel, auto server_channel) {
        channel_delivered = true;
      },
      dispatcher());
  l2cap_->TriggerOutboundChannel(kHandle1, l2cap::kRFCOMM, kL2CAPChannelId1,
                                 kL2CAPChannelId2);
  RunLoopUntilIdle();

  ExpectFrame(kHandle1, FrameType::kSetAsynchronousBalancedMode,
              kMuxControlDLCI);

  // Receive a UA on the session
  ReceiveFrame(kHandle1, std::make_unique<UnnumberedAcknowledgementResponse>(
                             state.role, kMuxControlDLCI));
  RunLoopUntilIdle();

  state.role = Role::kResponder;
  DLCI dlci = ServerChannelToDLCI(kMinServerChannel, OppositeRole(state.role));

  {
    // Expect a PN command from the session
    EXPECT_FALSE(handle_to_incoming_frames_.find(kHandle1) ==
                 handle_to_incoming_frames_.end());
    auto& queue = handle_to_incoming_frames_[kHandle1];
    EXPECT_EQ(1ul, queue.size());
    auto frame = Frame::Parse(state.credit_based_flow, OppositeRole(state.role),
                              queue.front()->view());
    queue.pop();
    EXPECT_EQ(FrameType::kUnnumberedInfoHeaderCheck,
              static_cast<FrameType>(frame->control()));
    DLCI dlci =
        ServerChannelToDLCI(kMinServerChannel, OppositeRole(state.role));
    auto mux_command =
        static_cast<MuxCommandFrame*>(frame.get())->TakeMuxCommand();
    EXPECT_EQ(MuxCommandType::kDLCParameterNegotiation,
              mux_command->command_type());

    // Create invalid parameters.
    auto params =
        static_cast<DLCParameterNegotiationCommand*>(mux_command.get())
            ->params();
    EXPECT_EQ(dlci, params.dlci);
    params.credit_based_flow_handshake =
        CreditBasedFlowHandshake::kSupportedResponse;
    // Request a larger max frame size than what was proposed.
    params.maximum_frame_size += 1;

    // Receive PN response
    ReceiveFrame(kHandle1, std::make_unique<MuxCommandFrame>(
                               OppositeRole(state.role), true,
                               std::make_unique<DLCParameterNegotiationCommand>(
                                   CommandResponse::kResponse, params)));
    RunLoopUntilIdle();
  }

  ExpectFrame(kHandle1, FrameType::kDisconnect, dlci);
}

// A DM response to a mux SABM shouldn't crash (but shouldn't do anything else).
TEST_F(RFCOMM_ChannelManagerTest,
       MuxStartupAndParamNegotiation_RejectMuxStartup) {
  AddFakePeerState(kHandle1, PeerState{true /*credits*/, Role::kUnassigned});

  bool channel_delivered = false;
  channel_manager_->OpenRemoteChannel(
      kHandle1, kMinServerChannel,
      [&channel_delivered](auto channel, auto server_channel) {
        channel_delivered = true;
      },
      dispatcher());
  l2cap_->TriggerOutboundChannel(kHandle1, l2cap::kRFCOMM, kL2CAPChannelId1,
                                 kL2CAPChannelId2);
  RunLoopUntilIdle();

  ExpectFrame(kHandle1, FrameType::kSetAsynchronousBalancedMode,
              kMuxControlDLCI);

  // Receive a DM on the session
  ReceiveFrame(kHandle1, std::make_unique<DisconnectedModeResponse>(
                             Role::kUnassigned, kMuxControlDLCI));
  RunLoopUntilIdle();
}

}  // namespace
}  // namespace rfcomm
}  // namespace btlib
