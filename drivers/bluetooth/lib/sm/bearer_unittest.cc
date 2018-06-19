// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bearer.h"

#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "garnet/drivers/bluetooth/lib/l2cap/fake_channel_test.h"

#include "lib/fxl/macros.h"

namespace btlib {
namespace sm {
namespace {

using common::BufferView;
using common::ByteBuffer;
using common::ContainersEqual;
using common::CreateStaticByteBuffer;
using common::DynamicByteBuffer;
using common::HostError;
using common::StaticByteBuffer;

class SMP_BearerTest : public l2cap::testing::FakeChannelTest {
 public:
  SMP_BearerTest() = default;
  ~SMP_BearerTest() override = default;

 protected:
  void SetUp() override { NewBearer(); }

  void TearDown() override { bearer_ = nullptr; }

  void NewBearer(hci::Connection::Role role = hci::Connection::Role::kMaster,
                 bool sc_supported = false,
                 IOCapability io_capability = IOCapability::kNoInputNoOutput) {
    ChannelOptions options(l2cap::kLESMPChannelId);
    fake_chan_ = CreateFakeChannel(options);
    bearer_ = std::make_unique<Bearer>(
        fake_chan_, role, sc_supported, io_capability,
        fit::bind_member(this, &SMP_BearerTest::OnPairingError),
        fit::bind_member(this, &SMP_BearerTest::OnFeatureExchangeComplete));
  }

  void OnPairingError(Status error) {
    pairing_error_count_++;
    last_error_ = error;
  }

  void OnFeatureExchangeComplete(const PairingFeatures& features,
                                 const ByteBuffer& preq,
                                 const ByteBuffer& pres) {
    feature_exchange_count_++;
    features_ = features;
    preq_ = DynamicByteBuffer(preq);
    pres_ = DynamicByteBuffer(pres);
  }

  Bearer* bearer() const { return bearer_.get(); }
  l2cap::testing::FakeChannel* fake_chan() const { return fake_chan_.get(); }

  int pairing_error_count() const { return pairing_error_count_; }
  Status last_error() const { return last_error_; }

  int feature_exchange_count() const { return feature_exchange_count_; }
  const PairingFeatures& features() const { return features_; }
  const ByteBuffer& preq() const { return preq_; }
  const ByteBuffer& pres() const { return pres_; }

 private:
  fbl::RefPtr<l2cap::testing::FakeChannel> fake_chan_;
  std::unique_ptr<Bearer> bearer_;

  int pairing_error_count_ = 0;
  Status last_error_;

  int feature_exchange_count_ = 0;
  PairingFeatures features_;
  DynamicByteBuffer pres_, preq_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SMP_BearerTest);
};

TEST_F(SMP_BearerTest, PacketsWhileIdle) {
  int tx_count = 0;
  fake_chan()->SetSendCallback([&](auto) { tx_count++; }, dispatcher());

  // Packets received while idle should have no side effect.
  fake_chan()->Receive(BufferView());                    // empty invalid buffer
  fake_chan()->Receive(StaticByteBuffer<kLEMTU + 1>());  // exceeds MTU
  fake_chan()->Receive(CreateStaticByteBuffer(kPairingFailed));
  fake_chan()->Receive(CreateStaticByteBuffer(kPairingResponse));

  AdvanceTimeBy(zx::sec(kPairingTimeout));
  RunLoopUntilIdle();

  EXPECT_EQ(0, tx_count);
  EXPECT_EQ(0, pairing_error_count());
  EXPECT_EQ(0, feature_exchange_count());

  // Abort should have no effect either.
  bearer()->Abort(ErrorCode::kPairingNotSupported);

  // Unrecognized packets should result in a PairingFailed packet.
  fake_chan()->Receive(CreateStaticByteBuffer(0xFF));
  RunLoopUntilIdle();

  EXPECT_EQ(1, tx_count);
  EXPECT_EQ(0, pairing_error_count());
  EXPECT_EQ(0, feature_exchange_count());
}

TEST_F(SMP_BearerTest, FeatureExchangeErrorSlave) {
  NewBearer(hci::Connection::Role::kSlave);
  EXPECT_FALSE(bearer()->InitiateFeatureExchange());
}

TEST_F(SMP_BearerTest, FeatureExchangeStartDefaultParams) {
  const auto kExpected =
      CreateStaticByteBuffer(0x01,  // code: "Pairing Request"
                             0x03,  // IO cap.: NoInputNoOutput
                             0x00,  // OOB: not present
                             0x01,  // AuthReq: bonding, no MITM
                             0x10,  // encr. key size: 16 (default max)
                             0x01,  // initator key dist.: encr. key only
                             0x01   // responder key dist.: encr. key only
      );

  int tx_count = 0;
  fake_chan()->SetSendCallback(
      [&](auto pdu) {
        tx_count++;
        EXPECT_TRUE(ContainersEqual(kExpected, *pdu));
      },
      dispatcher());
  EXPECT_TRUE(bearer()->InitiateFeatureExchange());

  RunLoopUntilIdle();

  EXPECT_EQ(1, tx_count);
  EXPECT_TRUE(bearer()->pairing_started());
  EXPECT_FALSE(bearer()->InitiateFeatureExchange());
}

TEST_F(SMP_BearerTest, FeatureExchangeStartCustomParams) {
  NewBearer(hci::Connection::Role::kMaster, true /* sc_supported */,
            IOCapability::kDisplayYesNo);
  bearer()->set_oob_available(true);
  bearer()->set_mitm_required(true);

  const auto kExpected =
      CreateStaticByteBuffer(0x01,        // code: "Pairing Request"
                             0x01,        // IO cap.: DisplayYesNo
                             0x01,        // OOB: present
                             0b00001101,  // AuthReq: Bonding, SC, MITM
                             0x10,        // encr. key size: 16 (default max)
                             0x01,        // initator key dist.: encr. key only
                             0x01         // responder key dist.: encr. key only
      );

  int tx_count = 0;
  fake_chan()->SetSendCallback(
      [&](auto pdu) {
        tx_count++;
        EXPECT_TRUE(ContainersEqual(kExpected, *pdu));
      },
      dispatcher());
  EXPECT_TRUE(bearer()->InitiateFeatureExchange());

  RunLoopUntilIdle();

  EXPECT_EQ(1, tx_count);
  EXPECT_TRUE(bearer()->pairing_started());
  EXPECT_FALSE(bearer()->InitiateFeatureExchange());
}

TEST_F(SMP_BearerTest, FeatureExchangeTimeout) {
  bearer()->InitiateFeatureExchange();
  ASSERT_TRUE(bearer()->pairing_started());

  AdvanceTimeBy(zx::sec(kPairingTimeout));
  RunLoopUntilIdle();

  EXPECT_EQ(HostError::kTimedOut, last_error().error());
  EXPECT_TRUE(fake_chan()->link_error());
  EXPECT_FALSE(bearer()->pairing_started());
  EXPECT_EQ(1, pairing_error_count());
  EXPECT_EQ(0, feature_exchange_count());
}

TEST_F(SMP_BearerTest, Abort) {
  bearer()->InitiateFeatureExchange();
  ASSERT_TRUE(bearer()->pairing_started());

  bearer()->Abort(ErrorCode::kPairingNotSupported);
  EXPECT_EQ(ErrorCode::kPairingNotSupported, last_error().protocol_error());
  EXPECT_FALSE(bearer()->pairing_started());
  EXPECT_FALSE(fake_chan()->link_error());
  EXPECT_EQ(1, pairing_error_count());
  EXPECT_EQ(0, feature_exchange_count());

  // Timer should have stopped.
  AdvanceTimeBy(zx::sec(kPairingTimeout));
  RunLoopUntilIdle();

  EXPECT_EQ(1, pairing_error_count());
}

TEST_F(SMP_BearerTest, FeatureExchangePairingFailed) {
  bearer()->InitiateFeatureExchange();
  ASSERT_TRUE(bearer()->pairing_started());

  fake_chan()->Receive(CreateStaticByteBuffer(
      0x05,  // code: Pairing Failed
      0x05   // reason: Pairing Not Supported
      ));
  RunLoopUntilIdle();

  EXPECT_FALSE(bearer()->pairing_started());
  EXPECT_EQ(1, pairing_error_count());
  EXPECT_EQ(0, feature_exchange_count());
  EXPECT_EQ(ErrorCode::kPairingNotSupported, last_error().protocol_error());
}

// Pairing should fail if MITM is required but the pairing method cannot provide
// it (due to I/O capabilities).
TEST_F(SMP_BearerTest, FeatureExchangeFailureAuthenticationRequirements) {
  const auto kRequest =
      CreateStaticByteBuffer(0x01,  // code: Pairing Request
                             0x03,  // IO cap.: NoInputNoOutput
                             0x00,  // OOB: not present
                             0x01,  // AuthReq: bonding, no MITM
                             0x10,  // encr. key size: 16 (default max)
                             0x01,  // initator key dist.: encr. key only
                             0x01   // responder key dist.: encr. key only
      );
  const auto kResponse =
      CreateStaticByteBuffer(0x02,  // code: Pairing Response
                             0x00,  // IO cap.: DisplayOnly
                             0x00,  // OOB: not present
                             0x04,  // AuthReq: MITM required
                             0x07,  // encr. key size: 7 (default min)
                             0x01,  // initator key dist.: encr. key only
                             0x01   // responder key dist.: encr. key only
      );
  const auto kFailure =
      CreateStaticByteBuffer(0x05,  // code: Pairing Failed
                             0x03   // reason: Authentication requirements
      );

  // Initiate the request in a loop task for Expect to detect it.
  async::PostTask(dispatcher(),
                  [this] { bearer()->InitiateFeatureExchange(); });
  ASSERT_TRUE(Expect(kRequest));
  ASSERT_TRUE(bearer()->pairing_started());

  // We should receive a pairing response and reply back with Pairing Failed.
  EXPECT_TRUE(ReceiveAndExpect(kResponse, kFailure));

  EXPECT_FALSE(bearer()->pairing_started());
  EXPECT_EQ(1, pairing_error_count());
  EXPECT_TRUE(last_error().is_protocol_error());
  EXPECT_EQ(ErrorCode::kAuthenticationRequirements,
            last_error().protocol_error());
  EXPECT_EQ(0, feature_exchange_count());
}

TEST_F(SMP_BearerTest, FeatureExchangePairingResponseJustWorks) {
  const auto kRequest =
      CreateStaticByteBuffer(0x01,  // code: Pairing Request
                             0x03,  // IO cap.: NoInputNoOutput
                             0x00,  // OOB: not present
                             0x01,  // AuthReq: bonding, MITM not required
                             0x10,  // encr. key size: 16 (default max)
                             0x01,  // initator key dist.: encr. key only
                             0x01   // responder key dist.: encr. key only
      );
  const auto kResponse =
      CreateStaticByteBuffer(0x02,  // code: Pairing Response
                             0x00,  // IO cap.: DisplayOnly
                             0x00,  // OOB: not present
                             0x00,  // AuthReq: MITM not required
                             0x07,  // encr. key size: 7 (default min)
                             0x01,  // initator key dist.: encr. key only
                             0x01   // responder key dist.: encr. key only
      );

  // Initiate the request in a loop task for Expect to detect it.
  async::PostTask(dispatcher(),
                  [this] { bearer()->InitiateFeatureExchange(); });
  ASSERT_TRUE(Expect(kRequest));
  ASSERT_TRUE(bearer()->pairing_started());

  fake_chan()->Receive(kResponse);
  RunLoopUntilIdle();

  // Pairing should continue until explicitly stopped.
  EXPECT_TRUE(bearer()->pairing_started());
  EXPECT_EQ(0, pairing_error_count());
  EXPECT_EQ(1, feature_exchange_count());

  EXPECT_TRUE(features().initiator);
  EXPECT_FALSE(features().secure_connections);
  EXPECT_EQ(PairingMethod::kJustWorks, features().method);
  EXPECT_EQ(7, features().encryption_key_size);
  EXPECT_TRUE(KeyDistGen::kEncKey & features().local_key_distribution);
  EXPECT_TRUE(KeyDistGen::kEncKey & features().remote_key_distribution);
  EXPECT_TRUE(ContainersEqual(kRequest, preq()));
  EXPECT_TRUE(ContainersEqual(kResponse, pres()));
}

// One of the devices requires MITM protection and the I/O capabilities can
// provide it.
TEST_F(SMP_BearerTest, FeatureExchangePairingResponseMITM) {
  NewBearer(hci::Connection::Role::kMaster, false /* sc_supported */,
            IOCapability::kDisplayYesNo);

  const auto kRequest =
      CreateStaticByteBuffer(0x01,  // code: Pairing Request
                             0x01,  // IO cap.: DisplayYesNo
                             0x00,  // OOB: not present
                             0x01,  // AuthReq: bonding, MITM not required
                             0x10,  // encr. key size: 16 (default max)
                             0x01,  // initator key dist.: encr. key only
                             0x01   // responder key dist.: encr. key only
      );
  const auto kResponse =
      CreateStaticByteBuffer(0x02,  // code: Pairing Response
                             0x02,  // IO cap.: KeyboardOnly
                             0x00,  // OOB: not present
                             0x04,  // AuthReq: MITM required
                             0x07,  // encr. key size: 7 (default min)
                             0x01,  // initator key dist.: encr. key only
                             0x01   // responder key dist.: encr. key only
      );

  // Initiate the request in a loop task for Expect to detect it.
  async::PostTask(dispatcher(),
                  [this] { bearer()->InitiateFeatureExchange(); });
  ASSERT_TRUE(Expect(kRequest));
  ASSERT_TRUE(bearer()->pairing_started());

  fake_chan()->Receive(kResponse);
  RunLoopUntilIdle();

  // Pairing should continue until explicitly stopped.
  EXPECT_TRUE(bearer()->pairing_started());
  EXPECT_EQ(0, pairing_error_count());
  EXPECT_EQ(1, feature_exchange_count());

  EXPECT_TRUE(features().initiator);
  EXPECT_FALSE(features().secure_connections);
  EXPECT_EQ(PairingMethod::kPasskeyEntry, features().method);
  EXPECT_EQ(7, features().encryption_key_size);
  EXPECT_TRUE(KeyDistGen::kEncKey & features().local_key_distribution);
  EXPECT_TRUE(KeyDistGen::kEncKey & features().remote_key_distribution);
  EXPECT_TRUE(ContainersEqual(kRequest, preq()));
  EXPECT_TRUE(ContainersEqual(kResponse, pres()));
}

TEST_F(SMP_BearerTest, FeatureExchangeEncryptionKeySize) {
  const auto kRequest =
      CreateStaticByteBuffer(0x01,  // code: Pairing Request
                             0x03,  // IO cap.: NoInputNoOutput
                             0x00,  // OOB: not present
                             0x01,  // AuthReq: bonding, no MITM
                             0x10,  // encr. key size: 16 (default max)
                             0x01,  // initator key dist.: encr. key only
                             0x01   // responder key dist.: encr. key only
      );
  const auto kResponse =
      CreateStaticByteBuffer(0x02,  // code: Pairing Response
                             0x00,  // IO cap.: DisplayOnly
                             0x00,  // OOB: not present
                             0x04,  // AuthReq: MITM required
                             0x02,  // encr. key size: 2 (too small)
                             0x01,  // initator key dist.: encr. key only
                             0x01   // responder key dist.: encr. key only
      );
  const auto kFailure =
      CreateStaticByteBuffer(0x05,  // code: Pairing Failed
                             0x06   // reason: Encryption Key Size
      );

  // Initiate the request in a loop task for Expect to detect it.
  async::PostTask(dispatcher(), [this] {
    bearer()->InitiateFeatureExchange();
    EXPECT_TRUE(bearer()->pairing_started());
  });
  ASSERT_TRUE(Expect(kRequest));
  ASSERT_TRUE(bearer()->pairing_started());

  // We should receive a pairing response and reply back with Pairing Failed.
  EXPECT_TRUE(ReceiveAndExpect(kResponse, kFailure));

  EXPECT_FALSE(bearer()->pairing_started());
  EXPECT_EQ(1, pairing_error_count());
  EXPECT_EQ(0, feature_exchange_count());
  EXPECT_EQ(ErrorCode::kEncryptionKeySize, last_error().protocol_error());
}

TEST_F(SMP_BearerTest, FeatureExchangeResponderErrorMaster) {
  const auto kRequest =
      CreateStaticByteBuffer(0x01,  // code: Pairing Request
                             0x03,  // IO cap.: NoInputNoOutput
                             0x00,  // OOB: not present
                             0x00,  // AuthReq: no auth. request by default
                             0x10,  // encr. key size: 16 (default max)
                             0x01,  // initator key dist.: encr. key only
                             0x01   // responder key dist.: encr. key only
      );
  const auto kFailure =
      CreateStaticByteBuffer(0x05,  // code: Pairing Failed
                             0x07   // reason: Command Not Supported
      );

  NewBearer(hci::Connection::Role::kMaster);
  EXPECT_TRUE(ReceiveAndExpect(kRequest, kFailure));
  EXPECT_FALSE(bearer()->pairing_started());
}

TEST_F(SMP_BearerTest, FeatureExchangeResponderMalformedRequest) {
  const auto kMalformedRequest =
      CreateStaticByteBuffer(0x01,  // code: Pairing Request
                             0x03,  // IO cap.: NoInputNoOutput
                             0x00,  // OOB: not present
                             0x00,  // AuthReq: no auth. request by default
                             0x10,  // encr. key size: 16 (default max)
                             0x01   // initator key dist.: encr. key only
                                    // Missing last byte
      );
  const auto kFailure =
      CreateStaticByteBuffer(0x05,  // code: Pairing Failed
                             0x0A   // reason: Invalid Parameters
      );

  NewBearer(hci::Connection::Role::kMaster);
  EXPECT_TRUE(ReceiveAndExpect(kMalformedRequest, kFailure));
  EXPECT_FALSE(bearer()->pairing_started());
}

// Tests that the pairing timer gets reset when a second Pairing Request is
// received.
TEST_F(SMP_BearerTest, FeatureExchangeResponderTimerRestarted) {
  NewBearer(hci::Connection::Role::kSlave);

  constexpr uint64_t kThresholdSeconds = 1;
  const auto kRequest =
      CreateStaticByteBuffer(0x01,  // code: Pairing Request
                             0x03,  // IO cap.: NoInputNoOutput
                             0x00,  // OOB: not present
                             0x00,  // AuthReq: no auth. request by default
                             0x10,  // encr. key size: 16 (default max)
                             0x01,  // initator key dist.: encr. key only
                             0x01   // responder key dist.: encr. key only
      );
  fake_chan()->Receive(kRequest);
  RunLoopUntilIdle();
  ASSERT_TRUE(bearer()->pairing_started());

  // Advance the time to 1 second behind the end of the pairing timeout.
  AdvanceTimeBy(zx::sec(kPairingTimeout - kThresholdSeconds));
  RunLoopUntilIdle();

  // The timer should not have expired.
  EXPECT_TRUE(bearer()->pairing_started());
  EXPECT_FALSE(fake_chan()->link_error());
  EXPECT_EQ(0, pairing_error_count());
  EXPECT_TRUE(last_error().is_success());

  // Send a second request which should restart the timer.
  fake_chan()->Receive(kRequest);
  RunLoopUntilIdle();
  EXPECT_TRUE(bearer()->pairing_started());

  // The old timeout should not expire when advance to 1 second behind the new
  // timeout.
  AdvanceTimeBy(zx::sec(kPairingTimeout - kThresholdSeconds));
  RunLoopUntilIdle();
  EXPECT_TRUE(bearer()->pairing_started());
  EXPECT_EQ(0, pairing_error_count());

  AdvanceTimeBy(zx::sec(kThresholdSeconds));
  RunLoopUntilIdle();
  EXPECT_FALSE(bearer()->pairing_started());
  EXPECT_EQ(1, pairing_error_count());
  EXPECT_EQ(HostError::kTimedOut, last_error().error());
}

// Pairing should fail if MITM is required but the pairing method cannot provide
// it (due to I/O capabilities).
TEST_F(SMP_BearerTest,
       FeatureExchangeResponderFailedAuthenticationRequirements) {
  NewBearer(hci::Connection::Role::kSlave);

  const auto kRequest =
      CreateStaticByteBuffer(0x01,  // code: Pairing Response
                             0x00,  // IO cap.: DisplayOnly
                             0x00,  // OOB: not present
                             0x04,  // AuthReq: MITM required
                             0x07,  // encr. key size: 7 (default min)
                             0x01,  // initator key dist.: encr. key only
                             0x01   // responder key dist.: encr. key only
      );
  const auto kFailure =
      CreateStaticByteBuffer(0x05,  // code: Pairing Failed
                             0x03   // reason: Authentication requirements
      );

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kFailure));
  EXPECT_EQ(1, pairing_error_count());
  EXPECT_FALSE(bearer()->pairing_started());
  EXPECT_EQ(ErrorCode::kAuthenticationRequirements,
            last_error().protocol_error());
}

TEST_F(SMP_BearerTest, FeatureExchangeResponderJustWorks) {
  NewBearer(hci::Connection::Role::kSlave);

  const auto kRequest =
      CreateStaticByteBuffer(0x01,  // code: Pairing Response
                             0x00,  // IO cap.: DisplayOnly
                             0x00,  // OOB: not present
                             0x00,  // AuthReq: MITM not required
                             0x07,  // encr. key size: 7 (default min)
                             0x01,  // initator key dist.: encr. key only
                             0x01   // responder key dist.: encr. key only
      );
  const auto kResponse =
      CreateStaticByteBuffer(0x02,  // code: Pairing Response
                             0x03,  // IO cap.: NoInputNoOutput
                             0x00,  // OOB: not present
                             0x01,  // AuthReq: bonding, no MITM
                             0x10,  // encr. key size: 16 (default max)
                             0x01,  // initator key dist.: encr. key only
                             0x01   // responder key dist.: encr. key only
      );

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kResponse));

  EXPECT_TRUE(bearer()->pairing_started());
  EXPECT_EQ(0, pairing_error_count());
  EXPECT_EQ(1, feature_exchange_count());
  EXPECT_FALSE(features().initiator);
  EXPECT_FALSE(features().secure_connections);
  EXPECT_EQ(PairingMethod::kJustWorks, features().method);
  EXPECT_EQ(7, features().encryption_key_size);
  EXPECT_TRUE(KeyDistGen::kEncKey & features().local_key_distribution);
  EXPECT_TRUE(KeyDistGen::kEncKey & features().remote_key_distribution);
  EXPECT_TRUE(ContainersEqual(kRequest, preq()));
  EXPECT_TRUE(ContainersEqual(kResponse, pres()));
}

TEST_F(SMP_BearerTest, FeatureExchangeResponderMITM) {
  NewBearer(hci::Connection::Role::kSlave, false /* sc_supported */,
            IOCapability::kDisplayYesNo);

  const auto kRequest =
      CreateStaticByteBuffer(0x01,  // code: Pairing Request
                             0x02,  // IO cap.: KeyboardOnly
                             0x00,  // OOB: not present
                             0x04,  // AuthReq: MITM required
                             0x07,  // encr. key size: 7 (default min)
                             0x01,  // initator key dist.: encr. key only
                             0x01   // responder key dist.: encr. key only
      );
  const auto kResponse =
      CreateStaticByteBuffer(0x02,  // code: Pairing Response
                             0x01,  // IO cap.: DisplayYesNo
                             0x00,  // OOB: not present
                             0x01,  // AuthReq: bonding, no MITM
                             0x10,  // encr. key size: 16 (default max)
                             0x01,  // initator key dist.: encr. key only
                             0x01   // responder key dist.: encr. key only
      );

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kResponse));

  EXPECT_TRUE(bearer()->pairing_started());
  EXPECT_EQ(0, pairing_error_count());
  EXPECT_EQ(1, feature_exchange_count());
  EXPECT_FALSE(features().initiator);
  EXPECT_FALSE(features().secure_connections);
  EXPECT_EQ(PairingMethod::kPasskeyEntry, features().method);
  EXPECT_EQ(7, features().encryption_key_size);
  EXPECT_TRUE(KeyDistGen::kEncKey & features().local_key_distribution);
  EXPECT_TRUE(KeyDistGen::kEncKey & features().remote_key_distribution);
  EXPECT_TRUE(ContainersEqual(kRequest, preq()));
  EXPECT_TRUE(ContainersEqual(kResponse, pres()));
}

TEST_F(SMP_BearerTest, UnsupportedCommandDuringPairing) {
  bearer()->InitiateFeatureExchange();
  ASSERT_TRUE(bearer()->pairing_started());

  const auto kExpected =
      CreateStaticByteBuffer(0x05,  // code: Pairing Failed
                             0x07   // reason: Command Not Supported
      );
  ReceiveAndExpect(CreateStaticByteBuffer(0xFF), kExpected);
  EXPECT_FALSE(bearer()->pairing_started());
}

TEST_F(SMP_BearerTest, StopTimer) {
  const auto kResponse =
      CreateStaticByteBuffer(0x02,  // code: Pairing Response
                             0x00,  // IO cap.: DisplayOnly
                             0x00,  // OOB: not present
                             0x00,  // AuthReq: MITM not required
                             0x07,  // encr. key size: 7 (default min)
                             0x01,  // initator key dist.: encr. key only
                             0x01   // responder key dist.: encr. key only
      );

  bearer()->InitiateFeatureExchange();
  ASSERT_TRUE(bearer()->pairing_started());

  fake_chan()->Receive(kResponse);
  RunLoopUntilIdle();

  // Pairing should continue until explicitly stopped.
  EXPECT_TRUE(bearer()->pairing_started());

  bearer()->StopTimer();
  EXPECT_FALSE(bearer()->pairing_started());

  AdvanceTimeBy(zx::sec(kPairingTimeout));
  RunLoopUntilIdle();
  EXPECT_EQ(0, pairing_error_count());
}

}  // namespace
}  // namespace sm
}  // namespace btlib
