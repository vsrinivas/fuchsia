// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/l2cap/bredr_dynamic_channel.h"

#include <lib/async/cpp/task.h>
#include <vector>

#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "garnet/drivers/bluetooth/lib/l2cap/fake_signaling_channel.h"
#include "lib/gtest/test_loop_fixture.h"

namespace btlib {
namespace l2cap {
namespace internal {
namespace {

using common::LowerBits;
using common::UpperBits;

// TODO(NET-1093): Add integration test with FakeChannelTest and
// BrEdrSignalingChannel using snooped connection data to verify signaling
// channel traffic.

constexpr uint16_t kPsm = 0x0001;
constexpr uint16_t kInvalidPsm = 0x0002;  // Valid PSMs are odd.
constexpr ChannelId kLocalCId = 0x0040;
constexpr ChannelId kRemoteCId = 0x60a3;
constexpr ChannelId kBadCId = 0x003f;  // Not a dynamic channel.

// Commands Reject

const common::ByteBuffer& kRejNotUnderstood = common::CreateStaticByteBuffer(
    // Reject Reason (Not Understood)
    0x00, 0x00);

// Connection Requests

const common::ByteBuffer& kConnReq = common::CreateStaticByteBuffer(
    // PSM
    LowerBits(kPsm), UpperBits(kPsm),

    // Source CID
    LowerBits(kLocalCId), UpperBits(kLocalCId));

const common::ByteBuffer& kInboundConnReq = common::CreateStaticByteBuffer(
    // PSM
    LowerBits(kPsm), UpperBits(kPsm),

    // Source CID
    LowerBits(kRemoteCId), UpperBits(kRemoteCId));

const common::ByteBuffer& kInboundInvalidPsmConnReq =
    common::CreateStaticByteBuffer(
        // PSM
        LowerBits(kInvalidPsm), UpperBits(kInvalidPsm),

        // Source CID
        LowerBits(kRemoteCId), UpperBits(kRemoteCId));

const common::ByteBuffer& kInboundBadCIdConnReq =
    common::CreateStaticByteBuffer(
        // PSM
        LowerBits(kPsm), UpperBits(kPsm),

        // Source CID
        LowerBits(kBadCId), UpperBits(kBadCId));

// Connection Responses

const common::ByteBuffer& kPendingConnRsp = common::CreateStaticByteBuffer(
    // Destination CID
    0x00, 0x00,

    // Source CID
    LowerBits(kLocalCId), UpperBits(kLocalCId),

    // Result (Pending)
    0x01, 0x00,

    // Status (Authorization Pending)
    0x02, 0x00);

const common::ByteBuffer& kPendingConnRspWithId =
    common::CreateStaticByteBuffer(
        // Destination CID (Wrong endianness but valid)
        UpperBits(kRemoteCId), LowerBits(kRemoteCId),

        // Source CID
        LowerBits(kLocalCId), UpperBits(kLocalCId),

        // Result (Pending)
        0x01, 0x00,

        // Status (Authorization Pending)
        0x02, 0x00);

const common::ByteBuffer& kOkConnRsp = common::CreateStaticByteBuffer(
    // Destination CID
    LowerBits(kRemoteCId), UpperBits(kRemoteCId),

    // Source CID
    LowerBits(kLocalCId), UpperBits(kLocalCId),

    // Result (Successful)
    0x00, 0x00,

    // Status (No further information available)
    0x00, 0x00);

const common::ByteBuffer& kInvalidConnRsp = common::CreateStaticByteBuffer(
    // Destination CID (Not a dynamic channel ID)
    LowerBits(kBadCId), UpperBits(kBadCId),

    // Source CID
    LowerBits(kLocalCId), UpperBits(kLocalCId),

    // Result (Successful)
    0x00, 0x00,

    // Status (No further information available)
    0x00, 0x00);

const common::ByteBuffer& kRejectConnRsp = common::CreateStaticByteBuffer(
    // Destination CID (Invalid)
    LowerBits(kInvalidChannelId), UpperBits(kInvalidChannelId),

    // Source CID
    LowerBits(kLocalCId), UpperBits(kLocalCId),

    // Result (No resources)
    0x04, 0x00,

    // Status (No further information available)
    0x00, 0x00);

const common::ByteBuffer& kInboundOkConnRsp = common::CreateStaticByteBuffer(
    // Destination CID
    LowerBits(kLocalCId), UpperBits(kLocalCId),

    // Source CID
    LowerBits(kRemoteCId), UpperBits(kRemoteCId),

    // Result (Successful)
    0x00, 0x00,

    // Status (No further information available)
    0x00, 0x00);

const common::ByteBuffer& kInboundBadPsmConnRsp =
    common::CreateStaticByteBuffer(
        // Destination CID (Invalid)
        0x00, 0x00,

        // Source CID
        LowerBits(kRemoteCId), UpperBits(kRemoteCId),

        // Result (PSM Not Supported)
        0x02, 0x00,

        // Status (No further information available)
        0x00, 0x00);

const common::ByteBuffer& kInboundBadCIdConnRsp =
    common::CreateStaticByteBuffer(
        // Destination CID (Invalid)
        0x00, 0x00,

        // Source CID
        LowerBits(kBadCId), UpperBits(kBadCId),

        // Result (Invalid Source CID)
        0x06, 0x00,

        // Status (No further information available)
        0x00, 0x00);

// Disconnection Requests

const common::ByteBuffer& kDisconReq = common::CreateStaticByteBuffer(
    // Destination CID
    LowerBits(kRemoteCId), UpperBits(kRemoteCId),

    // Source CID
    LowerBits(kLocalCId), UpperBits(kLocalCId));

const common::ByteBuffer& kInboundDisconReq = common::CreateStaticByteBuffer(
    // Destination CID
    LowerBits(kLocalCId), UpperBits(kLocalCId),

    // Source CID
    LowerBits(kRemoteCId), UpperBits(kRemoteCId));

// Disconnection Responses

const common::ByteBuffer& kInboundDisconRsp = kInboundDisconReq;

const common::ByteBuffer& kDisconRsp = kDisconReq;

// Configuration Requests

const common::ByteBuffer& kConfigReq = common::CreateStaticByteBuffer(
    // Destination CID
    LowerBits(kRemoteCId), UpperBits(kRemoteCId),

    // Flags
    0x00, 0x00);

const common::ByteBuffer& kInboundConfigReq = common::CreateStaticByteBuffer(
    // Destination CID
    LowerBits(kLocalCId), UpperBits(kLocalCId),

    // Flags
    0x00, 0x00);

// Configuration Responses

const common::ByteBuffer& kOkConfigRsp = common::CreateStaticByteBuffer(
    // Source CID
    LowerBits(kLocalCId), UpperBits(kLocalCId),

    // Flags
    0x00, 0x00,

    // Result (Successful)
    0x00, 0x00);

const common::ByteBuffer& kUnknownIdConfigRsp = common::CreateStaticByteBuffer(
    // Source CID (Invalid)
    LowerBits(kBadCId), UpperBits(kBadCId),

    // Flags
    0x00, 0x00,

    // Result (Successful)
    0x00, 0x00);

const common::ByteBuffer& kPendingConfigRsp = common::CreateStaticByteBuffer(
    // Source CID
    LowerBits(kRemoteCId), UpperBits(kRemoteCId),

    // Flags
    0x00, 0x00,

    // Result (Pending)
    0x04, 0x00);

const common::ByteBuffer& kInboundOkConfigRsp = common::CreateStaticByteBuffer(
    // Source CID
    LowerBits(kRemoteCId), UpperBits(kRemoteCId),

    // Flags
    0x00, 0x00,

    // Result (Successful)
    0x00, 0x00);

class L2CAP_BrEdrDynamicChannelTest : public ::gtest::TestLoopFixture {
 public:
  L2CAP_BrEdrDynamicChannelTest() = default;
  ~L2CAP_BrEdrDynamicChannelTest() override = default;

 protected:
  // Import types for brevity.
  using DynamicChannelCallback = DynamicChannelRegistry::DynamicChannelCallback;
  using ServiceRequestCallback = DynamicChannelRegistry::ServiceRequestCallback;

  // TestLoopFixture overrides
  void SetUp() override {
    TestLoopFixture::SetUp();
    channel_close_cb_ = nullptr;
    service_request_cb_ = nullptr;
    signaling_channel_ =
        std::make_unique<testing::FakeSignalingChannel>(dispatcher());
    registry_ = std::make_unique<BrEdrDynamicChannelRegistry>(
        sig(),
        fit::bind_member(this, &L2CAP_BrEdrDynamicChannelTest::OnChannelClose),
        fit::bind_member(this,
                         &L2CAP_BrEdrDynamicChannelTest::OnServiceRequest));
  }

  void TearDown() override {
    registry_ = nullptr;
    signaling_channel_ = nullptr;
    service_request_cb_ = nullptr;
    channel_close_cb_ = nullptr;
    TestLoopFixture::TearDown();
  }

  testing::FakeSignalingChannel* sig() const {
    return signaling_channel_.get();
  }

  BrEdrDynamicChannelRegistry* registry() const { return registry_.get(); }

  void set_channel_close_cb(DynamicChannelCallback close_cb) {
    channel_close_cb_ = std::move(close_cb);
  }

  void set_service_request_cb(ServiceRequestCallback service_request_cb) {
    service_request_cb_ = std::move(service_request_cb);
  }

 private:
  void OnChannelClose(const DynamicChannel* channel) {
    if (channel_close_cb_) {
      channel_close_cb_(channel);
    }
  }

  // Default to rejecting all service requests if no test callback is set.
  DynamicChannelCallback OnServiceRequest(PSM psm) {
    if (service_request_cb_) {
      return service_request_cb_(psm);
    }
    return nullptr;
  }

  DynamicChannelCallback channel_close_cb_;
  ServiceRequestCallback service_request_cb_;
  std::unique_ptr<testing::FakeSignalingChannel> signaling_channel_;
  std::unique_ptr<BrEdrDynamicChannelRegistry> registry_;

  FXL_DISALLOW_COPY_AND_ASSIGN(L2CAP_BrEdrDynamicChannelTest);
};

TEST_F(L2CAP_BrEdrDynamicChannelTest, FailConnectChannel) {
  sig()->AddOutbound(kConnectionRequest, kConnReq.view(),
                     std::make_pair(SignalingChannel::Status::kSuccess,
                                    kRejectConnRsp.view()));

  // Build channel and operate it directly to be able to inspect it in the
  // connected but not open state.
  auto channel =
      BrEdrDynamicChannel::MakeOutbound(registry(), sig(), kPsm, kLocalCId);
  EXPECT_FALSE(channel->IsConnected());
  EXPECT_FALSE(channel->IsOpen());
  EXPECT_EQ(kLocalCId, channel->local_cid());

  int open_result_cb_count = 0;
  auto open_result_cb = [&open_result_cb_count, &channel] {
    if (open_result_cb_count == 0) {
      EXPECT_FALSE(channel->IsConnected());
      EXPECT_FALSE(channel->IsOpen());
    }
    open_result_cb_count++;
  };
  int close_cb_count = 0;
  set_channel_close_cb([&close_cb_count](auto) { close_cb_count++; });

  channel->Open(std::move(open_result_cb));

  RunLoopUntilIdle();

  EXPECT_EQ(1, open_result_cb_count);
  EXPECT_FALSE(channel->IsConnected());
  EXPECT_FALSE(channel->IsOpen());
  EXPECT_EQ(kInvalidChannelId, channel->remote_cid());

  // A failed-to-open channel should not invoke the close callback.
  EXPECT_EQ(0, close_cb_count);

  // No disconnection transaction expected because the channel isn't actually
  // owned by the registry.
}

TEST_F(L2CAP_BrEdrDynamicChannelTest, ConnectChannelFailConfig) {
  sig()->AddOutbound(
      kConnectionRequest, kConnReq.view(),
      std::make_pair(SignalingChannel::Status::kSuccess, kOkConnRsp.view()));
  sig()->AddOutbound(kConfigurationRequest, kConfigReq.view(),
                     std::make_pair(SignalingChannel::Status::kReject,
                                    kRejNotUnderstood.view()));

  // Build channel and operate it directly to be able to inspect it in the
  // connected but not open state.
  auto channel =
      BrEdrDynamicChannel::MakeOutbound(registry(), sig(), kPsm, kLocalCId);
  EXPECT_FALSE(channel->IsConnected());
  EXPECT_FALSE(channel->IsOpen());
  EXPECT_EQ(kLocalCId, channel->local_cid());

  int open_result_cb_count = 0;
  auto open_result_cb = [&open_result_cb_count, &channel] {
    if (open_result_cb_count == 0) {
      EXPECT_TRUE(channel->IsConnected());
      EXPECT_FALSE(channel->IsOpen());
    }
    open_result_cb_count++;
  };
  int close_cb_count = 0;
  set_channel_close_cb([&close_cb_count](auto) { close_cb_count++; });

  channel->Open(std::move(open_result_cb));
  RunLoopUntilIdle();
  EXPECT_TRUE(channel->IsConnected());

  // A connected channel should have a valid remote channel ID.
  EXPECT_EQ(kRemoteCId, channel->remote_cid());

  EXPECT_FALSE(channel->IsOpen());
  EXPECT_EQ(1, open_result_cb_count);

  // A failed-to-open channel should not invoke the close callback.
  EXPECT_EQ(0, close_cb_count);

  // No disconnection transaction expected because the channel isn't actually
  // owned by the registry.
}

TEST_F(L2CAP_BrEdrDynamicChannelTest, ConnectChannelFailInvalidResponse) {
  sig()->AddOutbound(kConnectionRequest, kConnReq.view(),
                     std::make_pair(SignalingChannel::Status::kSuccess,
                                    kInvalidConnRsp.view()));

  // Build channel and operate it directly to be able to inspect it in the
  // connected but not open state.
  auto channel =
      BrEdrDynamicChannel::MakeOutbound(registry(), sig(), kPsm, kLocalCId);

  int open_result_cb_count = 0;
  auto open_result_cb = [&open_result_cb_count, &channel] {
    if (open_result_cb_count == 0) {
      EXPECT_FALSE(channel->IsConnected());
      EXPECT_FALSE(channel->IsOpen());
    }
    open_result_cb_count++;
  };
  int close_cb_count = 0;
  set_channel_close_cb([&close_cb_count](auto) { close_cb_count++; });

  channel->Open(std::move(open_result_cb));
  RunLoopUntilIdle();
  EXPECT_FALSE(channel->IsConnected());
  EXPECT_FALSE(channel->IsOpen());
  EXPECT_EQ(1, open_result_cb_count);
  EXPECT_EQ(0, close_cb_count);

  // No disconnection transaction expected because the channel isn't actually
  // owned by the registry.
}

TEST_F(L2CAP_BrEdrDynamicChannelTest, OpenAndLocalCloseChannel) {
  sig()->AddOutbound(
      kConnectionRequest, kConnReq.view(),
      std::make_pair(SignalingChannel::Status::kSuccess, kOkConnRsp.view()));
  sig()->AddOutbound(
      kConfigurationRequest, kConfigReq.view(),
      std::make_pair(SignalingChannel::Status::kSuccess, kOkConfigRsp.view()));
  sig()->AddOutbound(
      kDisconnectionRequest, kDisconReq.view(),
      std::make_pair(SignalingChannel::Status::kSuccess, kDisconRsp.view()));

  int open_cb_count = 0;
  auto open_cb = [&open_cb_count](auto chan) {
    if (open_cb_count == 0) {
      ASSERT_TRUE(chan);
      EXPECT_TRUE(chan->IsOpen());
      EXPECT_TRUE(chan->IsConnected());
      EXPECT_EQ(kLocalCId, chan->local_cid());
      EXPECT_EQ(kRemoteCId, chan->remote_cid());
    }
    open_cb_count++;
  };

  int close_cb_count = 0;
  set_channel_close_cb([&close_cb_count](auto chan) {
    EXPECT_TRUE(chan);
    close_cb_count++;
  });

  registry()->OpenOutbound(kPsm, std::move(open_cb));

  RunLoopUntilIdle();

  sig()->ReceiveExpect(kConfigurationRequest, kInboundConfigReq,
                       kInboundOkConfigRsp);

  EXPECT_EQ(1, open_cb_count);
  EXPECT_EQ(0, close_cb_count);

  registry()->CloseChannel(kLocalCId);
  RunLoopUntilIdle();

  EXPECT_EQ(1, open_cb_count);

  // Local channel closure shouldn't trigger the close callback.
  EXPECT_EQ(0, close_cb_count);

  // Repeated closure of the same channel should not have any effect.
  registry()->CloseChannel(kLocalCId);
  RunLoopUntilIdle();

  EXPECT_EQ(1, open_cb_count);
  EXPECT_EQ(0, close_cb_count);
}

TEST_F(L2CAP_BrEdrDynamicChannelTest, OpenAndRemoteCloseChannel) {
  sig()->AddOutbound(
      kConnectionRequest, kConnReq.view(),
      std::make_pair(SignalingChannel::Status::kSuccess, kOkConnRsp.view()));
  sig()->AddOutbound(
      kConfigurationRequest, kConfigReq.view(),
      std::make_pair(SignalingChannel::Status::kSuccess, kOkConfigRsp.view()));

  int open_cb_count = 0;
  auto open_cb = [&open_cb_count](auto chan) { open_cb_count++; };

  int close_cb_count = 0;
  set_channel_close_cb([&close_cb_count](auto chan) {
    ASSERT_TRUE(chan);
    EXPECT_FALSE(chan->IsOpen());
    EXPECT_FALSE(chan->IsConnected());
    EXPECT_EQ(kLocalCId, chan->local_cid());
    EXPECT_EQ(kRemoteCId, chan->remote_cid());
    close_cb_count++;
  });

  registry()->OpenOutbound(kPsm, std::move(open_cb));

  RunLoopUntilIdle();

  sig()->ReceiveExpect(kConfigurationRequest, kInboundConfigReq,
                       kInboundOkConfigRsp);

  EXPECT_EQ(1, open_cb_count);
  EXPECT_EQ(0, close_cb_count);

  sig()->ReceiveExpect(kDisconnectionRequest, kInboundDisconReq,
                       kInboundDisconRsp);

  EXPECT_EQ(1, open_cb_count);

  // Remote channel closure should trigger the close callback.
  EXPECT_EQ(1, close_cb_count);
}

TEST_F(L2CAP_BrEdrDynamicChannelTest, OpenChannelWithPendingConn) {
  sig()->AddOutbound(
      kConnectionRequest, kConnReq.view(),
      std::make_pair(SignalingChannel::Status::kSuccess,
                     kPendingConnRsp.view()),
      std::make_pair(SignalingChannel::Status::kSuccess, kOkConnRsp.view()));
  sig()->AddOutbound(
      kConfigurationRequest, kConfigReq.view(),
      std::make_pair(SignalingChannel::Status::kSuccess, kOkConfigRsp.view()));
  sig()->AddOutbound(
      kDisconnectionRequest, kDisconReq.view(),
      std::make_pair(SignalingChannel::Status::kSuccess, kDisconRsp.view()));

  int open_cb_count = 0;
  registry()->OpenOutbound(kPsm, [&open_cb_count](auto chan) {
    open_cb_count++;
    ASSERT_TRUE(chan);
    EXPECT_EQ(kLocalCId, chan->local_cid());
    EXPECT_EQ(kRemoteCId, chan->remote_cid());
  });

  RunLoopUntilIdle();

  sig()->ReceiveExpect(kConfigurationRequest, kInboundConfigReq,
                       kInboundOkConfigRsp);

  EXPECT_EQ(1, open_cb_count);
}

TEST_F(L2CAP_BrEdrDynamicChannelTest, OpenChannelMismatchConnRsp) {
  // The first Connection Response (pending) has a different ID than the final
  // Connection Response (success).
  sig()->AddOutbound(
      kConnectionRequest, kConnReq.view(),
      std::make_pair(SignalingChannel::Status::kSuccess,
                     kPendingConnRspWithId.view()),
      std::make_pair(SignalingChannel::Status::kSuccess, kOkConnRsp.view()));
  sig()->AddOutbound(
      kConfigurationRequest, kConfigReq.view(),
      std::make_pair(SignalingChannel::Status::kSuccess, kOkConfigRsp.view()));
  sig()->AddOutbound(
      kDisconnectionRequest, kDisconReq.view(),
      std::make_pair(SignalingChannel::Status::kSuccess, kDisconRsp.view()));

  int open_cb_count = 0;
  registry()->OpenOutbound(kPsm, [&open_cb_count](auto chan) {
    open_cb_count++;
    ASSERT_TRUE(chan);
    EXPECT_EQ(kLocalCId, chan->local_cid());
    EXPECT_EQ(kRemoteCId, chan->remote_cid());
  });

  RunLoopUntilIdle();

  sig()->ReceiveExpect(kConfigurationRequest, kInboundConfigReq,
                       kInboundOkConfigRsp);

  EXPECT_EQ(1, open_cb_count);
}

TEST_F(L2CAP_BrEdrDynamicChannelTest, OpenChannelConfigPending) {
  sig()->AddOutbound(
      kConnectionRequest, kConnReq.view(),
      std::make_pair(SignalingChannel::Status::kSuccess, kOkConnRsp.view()));
  sig()->AddOutbound(
      kConfigurationRequest, kConfigReq.view(),
      std::make_pair(SignalingChannel::Status::kSuccess,
                     kPendingConfigRsp.view()),
      std::make_pair(SignalingChannel::Status::kSuccess, kOkConfigRsp.view()));
  sig()->AddOutbound(
      kDisconnectionRequest, kDisconReq.view(),
      std::make_pair(SignalingChannel::Status::kSuccess, kDisconRsp.view()));

  int open_cb_count = 0;
  registry()->OpenOutbound(kPsm, [&open_cb_count](auto chan) {
    open_cb_count++;
    ASSERT_TRUE(chan);
    EXPECT_EQ(kLocalCId, chan->local_cid());
    EXPECT_EQ(kRemoteCId, chan->remote_cid());
  });

  RunLoopUntilIdle();

  sig()->ReceiveExpect(kConfigurationRequest, kInboundConfigReq,
                       kInboundOkConfigRsp);

  EXPECT_EQ(1, open_cb_count);
}

TEST_F(L2CAP_BrEdrDynamicChannelTest, OpenChannelConfigWrongId) {
  sig()->AddOutbound(
      kConnectionRequest, kConnReq.view(),
      std::make_pair(SignalingChannel::Status::kSuccess, kOkConnRsp.view()));
  sig()->AddOutbound(kConfigurationRequest, kConfigReq.view(),
                     std::make_pair(SignalingChannel::Status::kSuccess,
                                    kUnknownIdConfigRsp.view()));
  sig()->AddOutbound(
      kDisconnectionRequest, kDisconReq.view(),
      std::make_pair(SignalingChannel::Status::kSuccess, kDisconRsp.view()));

  int open_cb_count = 0;
  registry()->OpenOutbound(kPsm, [&open_cb_count](auto chan) {
    open_cb_count++;
    EXPECT_FALSE(chan);
  });

  RunLoopUntilIdle();

  sig()->ReceiveExpectRejectInvalidChannelId(
      kConfigurationRequest, kInboundConfigReq, kLocalCId, kInvalidChannelId);

  EXPECT_EQ(1, open_cb_count);
}

TEST_F(L2CAP_BrEdrDynamicChannelTest, InboundConnectionOk) {
  sig()->AddOutbound(
      kConfigurationRequest, kConfigReq.view(),
      std::make_pair(SignalingChannel::Status::kSuccess, kOkConfigRsp.view()));
  sig()->AddOutbound(
      kDisconnectionRequest, kDisconReq.view(),
      std::make_pair(SignalingChannel::Status::kSuccess, kDisconRsp.view()));

  int open_cb_count = 0;
  DynamicChannelCallback open_cb = [&open_cb_count](auto chan) {
    open_cb_count++;
    ASSERT_TRUE(chan);
    EXPECT_EQ(kPsm, chan->psm());
    EXPECT_EQ(kLocalCId, chan->local_cid());
    EXPECT_EQ(kRemoteCId, chan->remote_cid());
  };

  int service_request_cb_count = 0;
  ServiceRequestCallback service_request_cb =
      [&service_request_cb_count, open_cb = std::move(open_cb)](
          PSM psm) mutable -> DynamicChannelCallback {
    service_request_cb_count++;
    EXPECT_EQ(kPsm, psm);
    if (psm == kPsm) {
      return open_cb.share();
    }
    return nullptr;
  };

  set_service_request_cb(std::move(service_request_cb));

  int close_cb_count = 0;
  set_channel_close_cb([&close_cb_count](auto chan) {
    ASSERT_TRUE(chan);
    EXPECT_EQ(kLocalCId, chan->local_cid());
    EXPECT_EQ(kRemoteCId, chan->remote_cid());
    close_cb_count++;
  });

  sig()->ReceiveExpect(kConnectionRequest, kInboundConnReq, kInboundOkConnRsp);
  RunLoopUntilIdle();

  EXPECT_EQ(1, service_request_cb_count);
  EXPECT_EQ(0, open_cb_count);

  RunLoopUntilIdle();

  sig()->ReceiveExpect(kConfigurationRequest, kInboundConfigReq,
                       kInboundOkConfigRsp);

  EXPECT_EQ(1, service_request_cb_count);
  EXPECT_EQ(1, open_cb_count);

  registry()->CloseChannel(kLocalCId);
}

TEST_F(L2CAP_BrEdrDynamicChannelTest, InboundConnectionInvalidPsm) {
  ServiceRequestCallback service_request_cb =
      [](PSM psm) -> DynamicChannelCallback {
    // Write user code that accepts the invalid PSM, but control flow may not
    // reach here.
    EXPECT_EQ(kInvalidPsm, psm);
    if (psm == kInvalidPsm) {
      return [](auto) { FAIL() << "Channel should fail to open for PSM"; };
    }
    return nullptr;
  };

  set_service_request_cb(std::move(service_request_cb));

  sig()->ReceiveExpect(kConnectionRequest, kInboundInvalidPsmConnReq,
                       kInboundBadPsmConnRsp);
  RunLoopUntilIdle();
}

TEST_F(L2CAP_BrEdrDynamicChannelTest, InboundConnectionUnsupportedPsm) {
  int service_request_cb_count = 0;
  ServiceRequestCallback service_request_cb =
      [&service_request_cb_count](PSM psm) -> DynamicChannelCallback {
    service_request_cb_count++;
    EXPECT_EQ(kPsm, psm);

    // Reject the service request.
    return nullptr;
  };

  set_service_request_cb(std::move(service_request_cb));

  sig()->ReceiveExpect(kConnectionRequest, kInboundConnReq,
                       kInboundBadPsmConnRsp);
  RunLoopUntilIdle();

  EXPECT_EQ(1, service_request_cb_count);
}

TEST_F(L2CAP_BrEdrDynamicChannelTest, InboundConnectionInvalidSrcCId) {
  ServiceRequestCallback service_request_cb =
      [](PSM psm) -> DynamicChannelCallback {
    // Control flow may not reach here.
    EXPECT_EQ(kPsm, psm);
    if (psm == kPsm) {
      return [](auto) { FAIL() << "Channel from src_cid should fail to open"; };
    }
    return nullptr;
  };

  set_service_request_cb(std::move(service_request_cb));

  sig()->ReceiveExpect(kConnectionRequest, kInboundBadCIdConnReq,
                       kInboundBadCIdConnRsp);
  RunLoopUntilIdle();
}

}  // namespace
}  // namespace internal
}  // namespace l2cap
}  // namespace btlib
