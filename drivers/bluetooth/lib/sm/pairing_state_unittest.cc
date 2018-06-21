// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pairing_state.h"

#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "garnet/drivers/bluetooth/lib/l2cap/fake_channel_test.h"
#include "garnet/drivers/bluetooth/lib/sm/packet.h"

#include "util.h"

#include "lib/fxl/macros.h"
#include "lib/fxl/random/rand.h"

namespace btlib {

using common::ByteBuffer;
using common::DeviceAddress;
using common::StaticByteBuffer;
using common::UInt128;

namespace sm {
namespace {

const DeviceAddress kLocalAddr(DeviceAddress::Type::kLEPublic,
                               "A1:A2:A3:A4:A5:A6");
const DeviceAddress kPeerAddr(DeviceAddress::Type::kLERandom,
                              "B1:B2:B3:B4:B5:B6");

class SMP_PairingStateTest : public l2cap::testing::FakeChannelTest {
 public:
  SMP_PairingStateTest() = default;
  ~SMP_PairingStateTest() override = default;

 protected:
  void TearDown() override {
    RunLoopUntilIdle();
    pairing_ = nullptr;
  }

  void NewPairingState(IOCapability ioc) {
    pairing_ = std::make_unique<PairingState>(ioc);
  }

  void RegisterLE(hci::Connection::Role role) {
    FXL_DCHECK(pairing_);

    ChannelOptions options(l2cap::kLESMPChannelId);
    fake_chan_ = CreateFakeChannel(options);
    fake_chan_->SetSendCallback(
        fit::bind_member(this, &SMP_PairingStateTest::OnDataReceived),
        dispatcher());
    pairing_->RegisterLE(fake_chan_, role, kLocalAddr, kPeerAddr);
  }

  void UpdateSecurity(SecurityLevel level) {
    FXL_DCHECK(pairing_);
    pairing_->UpdateSecurity(level, [this](auto status, const auto& props) {
      pairing_callback_count_++;
      pairing_status_ = status;
      sec_props_ = props;
    });
  }

  // Called when SMP sends a packet over the fake channel.
  void OnDataReceived(std::unique_ptr<const ByteBuffer> packet) {
    FXL_DCHECK(packet);

    PacketReader reader(packet.get());
    switch (reader.code()) {
      case kPairingFailed:
        pairing_failed_count_++;
        received_error_code_ = reader.payload<PairingFailedParams>();
        break;
      case kPairingRequest:
        pairing_request_count_++;
        packet->Copy(&local_pairing_cmd_);
        break;
      case kPairingResponse:
        pairing_response_count_++;
        packet->Copy(&local_pairing_cmd_);
        break;
      case kPairingConfirm:
        pairing_confirm_count_++;
        pairing_confirm_ = reader.payload<PairingConfirmValue>();
        break;
      case kPairingRandom:
        pairing_random_count_++;
        pairing_random_ = reader.payload<PairingRandomValue>();
        break;
      default:
        FAIL() << "Sent unsupported SMP command";
    }
  }

  // Emulates the receipt of pairing features (both as initiator and responder).
  void ReceivePairingFeatures(const PairingRequestParams& params,
                              bool peer_initiator = false) {
    PacketWriter writer(peer_initiator ? kPairingRequest : kPairingResponse,
                        &peer_pairing_cmd_);
    *writer.mutable_payload<PairingRequestParams>() = params;
    fake_chan()->Receive(peer_pairing_cmd_);
  }

  void ReceivePairingFeatures(IOCapability ioc = IOCapability::kNoInputNoOutput,
                              AuthReqField auth_req = 0,
                              uint8_t max_enc_key_size = kMaxEncryptionKeySize,
                              bool peer_initiator = false) {
    PairingRequestParams pairing_params;
    std::memset(&pairing_params, 0, sizeof(pairing_params));
    pairing_params.io_capability = ioc;
    pairing_params.auth_req = auth_req;
    pairing_params.max_encryption_key_size = max_enc_key_size;

    ReceivePairingFeatures(pairing_params, peer_initiator);
  }

  void ReceivePairingFailed(ErrorCode error_code) {
    StaticByteBuffer<sizeof(Header) + sizeof(ErrorCode)> buffer;
    PacketWriter writer(kPairingFailed, &buffer);
    *writer.mutable_payload<PairingFailedParams>() = error_code;
    fake_chan()->Receive(buffer);
  }

  void ReceivePairingConfirm(const UInt128& confirm) {
    Receive128BitCmd(kPairingConfirm, confirm);
  }

  void ReceivePairingRandom(const UInt128& random) {
    Receive128BitCmd(kPairingRandom, random);
  }

  void GenerateConfirmValue(const UInt128& random, UInt128* out_value,
                            bool peer_initiator = false) {
    FXL_DCHECK(out_value);
    UInt128 tk;
    tk.fill(0);

    const ByteBuffer *preq, *pres;
    const DeviceAddress *init_addr, *rsp_addr;
    if (peer_initiator) {
      preq = &peer_pairing_cmd();
      pres = &local_pairing_cmd();
      init_addr = &kPeerAddr;
      rsp_addr = &kLocalAddr;
    } else {
      preq = &local_pairing_cmd();
      pres = &peer_pairing_cmd();
      init_addr = &kLocalAddr;
      rsp_addr = &kPeerAddr;
    }

    util::C1(tk, random, *preq, *pres, *init_addr, *rsp_addr, out_value);
  }

  PairingState* pairing() const { return pairing_.get(); }
  l2cap::testing::FakeChannel* fake_chan() const { return fake_chan_.get(); }

  int pairing_callback_count() const { return pairing_callback_count_; }
  ErrorCode received_error_code() const { return received_error_code_; }
  const Status& pairing_status() const { return pairing_status_; }
  const SecurityProperties& sec_props() const { return sec_props_; }

  int pairing_failed_count() const { return pairing_failed_count_; }
  int pairing_request_count() const { return pairing_request_count_; }
  int pairing_response_count() const { return pairing_response_count_; }
  int pairing_confirm_count() const { return pairing_confirm_count_; }
  int pairing_random_count() const { return pairing_random_count_; }

  const UInt128& pairing_confirm() const { return pairing_confirm_; }
  const UInt128& pairing_random() const { return pairing_random_; }

  const ByteBuffer& local_pairing_cmd() const { return local_pairing_cmd_; }
  const ByteBuffer& peer_pairing_cmd() const { return peer_pairing_cmd_; }

 private:
  void Receive128BitCmd(Code cmd_code, const UInt128& value) {
    StaticByteBuffer<sizeof(Header) + sizeof(UInt128)> buffer;
    PacketWriter writer(cmd_code, &buffer);
    *writer.mutable_payload<UInt128>() = value;
    fake_chan()->Receive(buffer);
  }

  // We store the preq/pres values here to generate a valid confirm value for
  // the fake side.
  StaticByteBuffer<sizeof(Header) + sizeof(PairingRequestParams)>
      local_pairing_cmd_, peer_pairing_cmd_;

  // Number of times the security callback given to UpdateSecurity has been
  // called and the most recent parameters that it was called with.
  int pairing_callback_count_ = 0;
  Status pairing_status_;
  SecurityProperties sec_props_;

  // Counts of commands that we have sent out to the peer.
  int pairing_failed_count_ = 0;
  int pairing_request_count_ = 0;
  int pairing_response_count_ = 0;
  int pairing_confirm_count_ = 0;
  int pairing_random_count_ = 0;

  // Values that have been sent by the peer.
  UInt128 pairing_confirm_;
  UInt128 pairing_random_;
  ErrorCode received_error_code_ = ErrorCode::kNoError;

  fbl::RefPtr<l2cap::testing::FakeChannel> fake_chan_;
  std::unique_ptr<PairingState> pairing_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SMP_PairingStateTest);
};

class SMP_MasterPairingTest : public SMP_PairingStateTest {
 public:
  SMP_MasterPairingTest() = default;
  ~SMP_MasterPairingTest() override = default;

  void SetUp() override { SetUpPairingState(); }

  void SetUpPairingState(IOCapability ioc = IOCapability::kDisplayOnly) {
    NewPairingState(ioc);
    RegisterLE(hci::Connection::Role::kMaster);
  }

  void GenerateMatchingConfirmAndRandom(UInt128* out_confirm,
                                        UInt128* out_random) {
    FXL_DCHECK(out_confirm);
    FXL_DCHECK(out_random);
    fxl::RandBytes(out_random->data(), out_random->size());
    GenerateConfirmValue(*out_random, out_confirm);
  }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(SMP_MasterPairingTest);
};

class SMP_SlavePairingTest : public SMP_PairingStateTest {
 public:
  SMP_SlavePairingTest() = default;
  ~SMP_SlavePairingTest() override = default;

  void SetUp() override { SetUpPairingState(); }

  void SetUpPairingState(IOCapability ioc = IOCapability::kDisplayOnly) {
    NewPairingState(ioc);
    RegisterLE(hci::Connection::Role::kSlave);
  }

  void GenerateMatchingConfirmAndRandom(UInt128* out_confirm,
                                        UInt128* out_random) {
    FXL_DCHECK(out_confirm);
    FXL_DCHECK(out_random);
    fxl::RandBytes(out_random->data(), out_random->size());
    GenerateConfirmValue(*out_random, out_confirm, true /* peer_initiator */);
  }

  void ReceivePairingRequest(IOCapability ioc = IOCapability::kNoInputNoOutput,
                             AuthReqField auth_req = 0,
                             uint8_t max_enc_key_size = kMaxEncryptionKeySize) {
    ReceivePairingFeatures(ioc, auth_req, max_enc_key_size,
                           true /* peer_initiator */);
  }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(SMP_SlavePairingTest);
};

// Requesting pairing at the current security level should succeed immediately.
TEST_F(SMP_MasterPairingTest, UpdateSecurityCurrentLevel) {
  UpdateSecurity(SecurityLevel::kNoSecurity);
  RunLoopUntilIdle();

  // No pairing requests should have been made.
  EXPECT_EQ(0, pairing_request_count());

  // Pairing should succeed.
  EXPECT_EQ(1, pairing_callback_count());
  EXPECT_TRUE(pairing_status());
  EXPECT_EQ(SecurityLevel::kNoSecurity, sec_props().level());
  EXPECT_EQ(0u, sec_props().enc_key_size());
  EXPECT_FALSE(sec_props().secure_connections());
}

// Peer aborts during Phase 1.
TEST_F(SMP_MasterPairingTest, PairingFailedInPhase1) {
  UpdateSecurity(SecurityLevel::kEncrypted);
  RunLoopUntilIdle();

  // Pairing not complete yet but we should be in Phase 1.
  EXPECT_EQ(0, pairing_callback_count());
  EXPECT_EQ(1, pairing_request_count());

  ReceivePairingFailed(ErrorCode::kPairingNotSupported);
  RunLoopUntilIdle();

  EXPECT_EQ(1, pairing_callback_count());
  EXPECT_EQ(1, pairing_request_count());
  EXPECT_EQ(ErrorCode::kPairingNotSupported, pairing_status().protocol_error());
}

// Reject pairing if not using JustWorks.
// TODO(armansito): This is temporary but the test here to document the interim
// behavior until the TK gets obtained asynchronously.
TEST_F(SMP_MasterPairingTest, RejectIfNotJustWorks) {
  UpdateSecurity(SecurityLevel::kEncrypted);
  RunLoopUntilIdle();

  // Pick I/O capabilities and MITM flags that will result in authenticated
  // pairing.
  ReceivePairingFeatures(IOCapability::kKeyboardOnly, AuthReq::kMITM);
  RunLoopUntilIdle();

  EXPECT_EQ(1, pairing_callback_count());
  EXPECT_EQ(1, pairing_request_count());
  EXPECT_EQ(ErrorCode::kUnspecifiedReason, pairing_status().protocol_error());
}

TEST_F(SMP_MasterPairingTest, ReceiveConfirmValueWhileNotPairing) {
  UInt128 confirm;
  ReceivePairingConfirm(confirm);
  RunLoopUntilIdle();

  // Nothing should happen.
  EXPECT_EQ(0, pairing_confirm_count());
  EXPECT_EQ(0, pairing_random_count());
  EXPECT_EQ(0, pairing_failed_count());
  EXPECT_EQ(0, pairing_callback_count());
}

TEST_F(SMP_MasterPairingTest, ReceiveConfirmValueInPhase1) {
  UpdateSecurity(SecurityLevel::kEncrypted);
  RunLoopUntilIdle();

  UInt128 confirm;
  ReceivePairingConfirm(confirm);
  RunLoopUntilIdle();

  EXPECT_EQ(0, pairing_confirm_count());
  EXPECT_EQ(0, pairing_random_count());
  EXPECT_EQ(1, pairing_failed_count());
  EXPECT_EQ(1, pairing_callback_count());
  EXPECT_EQ(ErrorCode::kUnspecifiedReason, pairing_status().protocol_error());
}

TEST_F(SMP_MasterPairingTest, ReceiveRandomValueWhileNotPairing) {
  UInt128 random;
  ReceivePairingRandom(random);
  RunLoopUntilIdle();

  // Nothing should happen.
  EXPECT_EQ(0, pairing_confirm_count());
  EXPECT_EQ(0, pairing_random_count());
  EXPECT_EQ(0, pairing_failed_count());
  EXPECT_EQ(0, pairing_callback_count());
}

TEST_F(SMP_MasterPairingTest, ReceiveRandomValueInPhase1) {
  UpdateSecurity(SecurityLevel::kEncrypted);
  RunLoopUntilIdle();

  UInt128 random;
  ReceivePairingRandom(random);
  RunLoopUntilIdle();

  EXPECT_EQ(0, pairing_confirm_count());
  EXPECT_EQ(0, pairing_random_count());
  EXPECT_EQ(1, pairing_failed_count());
  EXPECT_EQ(1, pairing_callback_count());
  EXPECT_EQ(ErrorCode::kUnspecifiedReason, pairing_status().protocol_error());
}

TEST_F(SMP_MasterPairingTest, LegacyPhase2SlaveConfirmValueReceivedTwice) {
  UpdateSecurity(SecurityLevel::kEncrypted);
  RunLoopUntilIdle();

  ReceivePairingFeatures();
  RunLoopUntilIdle();

  // Should have received Mconfirm.
  EXPECT_EQ(1, pairing_confirm_count());
  EXPECT_EQ(0, pairing_random_count());
  EXPECT_EQ(0, pairing_callback_count());

  UInt128 confirm;
  ReceivePairingConfirm(confirm);
  RunLoopUntilIdle();

  // Should have received Mrand.
  EXPECT_EQ(1, pairing_confirm_count());
  EXPECT_EQ(1, pairing_random_count());
  EXPECT_EQ(0, pairing_callback_count());

  // Send Mconfirm again
  ReceivePairingConfirm(confirm);
  RunLoopUntilIdle();

  EXPECT_EQ(1, pairing_confirm_count());
  EXPECT_EQ(1, pairing_random_count());
  EXPECT_EQ(1, pairing_failed_count());
  EXPECT_EQ(1, pairing_callback_count());
  EXPECT_EQ(ErrorCode::kUnspecifiedReason, pairing_status().protocol_error());
}

TEST_F(SMP_MasterPairingTest, LegacyPhase2ReceiveRandomValueInWrongOrder) {
  UpdateSecurity(SecurityLevel::kEncrypted);
  RunLoopUntilIdle();

  ReceivePairingFeatures();
  RunLoopUntilIdle();

  // Should have received Mconfirm.
  EXPECT_EQ(1, pairing_confirm_count());
  EXPECT_EQ(0, pairing_random_count());
  EXPECT_EQ(0, pairing_callback_count());

  UInt128 random;
  ReceivePairingRandom(random);
  RunLoopUntilIdle();

  // Should have aborted pairing if Srand arrives before Srand.
  EXPECT_EQ(1, pairing_confirm_count());
  EXPECT_EQ(0, pairing_random_count());
  EXPECT_EQ(1, pairing_failed_count());
  EXPECT_EQ(1, pairing_callback_count());
  EXPECT_EQ(ErrorCode::kUnspecifiedReason, pairing_status().protocol_error());
}

TEST_F(SMP_MasterPairingTest, LegacyPhase2SlaveConfirmValueInvalid) {
  UpdateSecurity(SecurityLevel::kEncrypted);
  RunLoopUntilIdle();

  // Pick I/O capabilities and MITM flags that will result in Just Works
  // pairing.
  ReceivePairingFeatures();
  RunLoopUntilIdle();

  // Should have received Mconfirm.
  EXPECT_EQ(1, pairing_confirm_count());
  EXPECT_EQ(0, pairing_random_count());
  EXPECT_EQ(0, pairing_callback_count());

  // Receive Sconfirm and Srand values that don't match.
  UInt128 confirm, random;
  confirm.fill(0);
  random.fill(1);

  ReceivePairingConfirm(confirm);
  RunLoopUntilIdle();

  // Should have received Mrand.
  EXPECT_EQ(1, pairing_confirm_count());
  EXPECT_EQ(1, pairing_random_count());
  EXPECT_EQ(0, pairing_callback_count());

  // Master's Mconfirm/Mrand should be correct.
  UInt128 expected_confirm;
  GenerateConfirmValue(pairing_random(), &expected_confirm);
  EXPECT_EQ(expected_confirm, pairing_confirm());

  // Send the non-matching Srandom.
  ReceivePairingRandom(random);
  RunLoopUntilIdle();

  EXPECT_EQ(1, pairing_confirm_count());
  EXPECT_EQ(1, pairing_random_count());
  EXPECT_EQ(1, pairing_failed_count());
  EXPECT_EQ(1, pairing_callback_count());
  EXPECT_EQ(ErrorCode::kConfirmValueFailed, pairing_status().protocol_error());
}

TEST_F(SMP_MasterPairingTest, LegacyPhase2RandomValueReceivedTwice) {
  UpdateSecurity(SecurityLevel::kEncrypted);
  RunLoopUntilIdle();

  // Pick I/O capabilities and MITM flags that will result in Just Works
  // pairing.
  ReceivePairingFeatures();
  RunLoopUntilIdle();

  // Should have received Mconfirm.
  EXPECT_EQ(1, pairing_confirm_count());
  EXPECT_EQ(0, pairing_random_count());
  EXPECT_EQ(0, pairing_callback_count());

  // Receive Sconfirm and Srand values that match.
  UInt128 confirm, random;
  GenerateMatchingConfirmAndRandom(&confirm, &random);

  ReceivePairingConfirm(confirm);
  RunLoopUntilIdle();

  // Should have received Mrand.
  EXPECT_EQ(1, pairing_confirm_count());
  EXPECT_EQ(1, pairing_random_count());
  EXPECT_EQ(0, pairing_callback_count());

  // Master's Mconfirm/Mrand should be correct.
  UInt128 expected_confirm;
  GenerateConfirmValue(pairing_random(), &expected_confirm);
  EXPECT_EQ(expected_confirm, pairing_confirm());

  // Send Srandom.
  ReceivePairingRandom(random);
  RunLoopUntilIdle();

  EXPECT_EQ(1, pairing_confirm_count());
  EXPECT_EQ(1, pairing_random_count());
  EXPECT_EQ(0, pairing_failed_count());
  EXPECT_EQ(0, pairing_callback_count());

  // Send Srandom again.
  ReceivePairingRandom(random);
  RunLoopUntilIdle();

  EXPECT_EQ(1, pairing_confirm_count());
  EXPECT_EQ(1, pairing_random_count());
  EXPECT_EQ(1, pairing_failed_count());
  EXPECT_EQ(1, pairing_callback_count());
  EXPECT_EQ(ErrorCode::kUnspecifiedReason, pairing_status().protocol_error());
}

TEST_F(SMP_MasterPairingTest, LegacyPhase2ConfirmValuesExchanged) {
  UpdateSecurity(SecurityLevel::kEncrypted);
  RunLoopUntilIdle();

  // Pick I/O capabilities and MITM flags that will result in Just Works
  // pairing.
  ReceivePairingFeatures();
  RunLoopUntilIdle();

  // Should have received Mconfirm.
  EXPECT_EQ(1, pairing_confirm_count());
  EXPECT_EQ(0, pairing_random_count());
  EXPECT_EQ(0, pairing_callback_count());

  // Receive Sconfirm and Srand values that match.
  UInt128 confirm, random;
  GenerateMatchingConfirmAndRandom(&confirm, &random);

  ReceivePairingConfirm(confirm);
  RunLoopUntilIdle();

  // Should have received Mrand.
  EXPECT_EQ(1, pairing_confirm_count());
  EXPECT_EQ(1, pairing_random_count());
  EXPECT_EQ(0, pairing_callback_count());

  // Master's Mconfirm/Mrand should be correct.
  UInt128 expected_confirm;
  GenerateConfirmValue(pairing_random(), &expected_confirm);
  EXPECT_EQ(expected_confirm, pairing_confirm());

  // Send Srandom.
  ReceivePairingRandom(random);
  RunLoopUntilIdle();

  EXPECT_EQ(1, pairing_confirm_count());
  EXPECT_EQ(1, pairing_random_count());
  EXPECT_EQ(0, pairing_failed_count());
  EXPECT_EQ(0, pairing_callback_count());
}

// Peer aborts during Phase 2.
TEST_F(SMP_MasterPairingTest, PairingFailedInPhase2) {
  UpdateSecurity(SecurityLevel::kEncrypted);
  ReceivePairingFeatures();
  RunLoopUntilIdle();

  UInt128 confirm, random;
  GenerateMatchingConfirmAndRandom(&confirm, &random);

  ReceivePairingConfirm(confirm);
  RunLoopUntilIdle();

  EXPECT_EQ(1, pairing_confirm_count());
  EXPECT_EQ(1, pairing_random_count());
  EXPECT_EQ(0, pairing_failed_count());
  EXPECT_EQ(0, pairing_callback_count());

  ReceivePairingFailed(ErrorCode::kConfirmValueFailed);
  RunLoopUntilIdle();

  EXPECT_EQ(1, pairing_confirm_count());
  EXPECT_EQ(1, pairing_random_count());
  EXPECT_EQ(0, pairing_failed_count());
  EXPECT_EQ(1, pairing_callback_count());
  EXPECT_EQ(ErrorCode::kConfirmValueFailed, pairing_status().protocol_error());
}

TEST_F(SMP_SlavePairingTest, ReceiveSecondPairingRequestWhilePairing) {
  ReceivePairingRequest();
  RunLoopUntilIdle();

  // We should have sent a pairing response and should now be in Phase 2,
  // waiting for the peer to send us Mconfirm.
  EXPECT_EQ(0, pairing_request_count());
  EXPECT_EQ(1, pairing_response_count());
  EXPECT_EQ(0, pairing_confirm_count());
  EXPECT_EQ(0, pairing_random_count());
  EXPECT_EQ(0, pairing_failed_count());
  EXPECT_EQ(0, pairing_callback_count());

  // This should cause pairing to be aborted.
  ReceivePairingRequest();
  RunLoopUntilIdle();
  EXPECT_EQ(0, pairing_request_count());
  EXPECT_EQ(2, pairing_response_count());
  EXPECT_EQ(0, pairing_confirm_count());
  EXPECT_EQ(0, pairing_random_count());
  EXPECT_EQ(1, pairing_failed_count());
  EXPECT_EQ(0, pairing_callback_count());
  EXPECT_EQ(ErrorCode::kUnspecifiedReason, received_error_code());
}

TEST_F(SMP_SlavePairingTest, LegacyPhase2ReceivePairingRandomInWrongOrder) {
  ReceivePairingRequest();
  RunLoopUntilIdle();

  // We should have sent a pairing response and should now be in Phase 2,
  // waiting for the peer to send us Mconfirm.
  EXPECT_EQ(0, pairing_request_count());
  EXPECT_EQ(1, pairing_response_count());
  EXPECT_EQ(0, pairing_confirm_count());
  EXPECT_EQ(0, pairing_random_count());
  EXPECT_EQ(0, pairing_failed_count());
  EXPECT_EQ(0, pairing_callback_count());

  // Master sends Mrand before Mconfirm.
  UInt128 random;
  ReceivePairingRandom(random);
  RunLoopUntilIdle();
  EXPECT_EQ(0, pairing_request_count());
  EXPECT_EQ(1, pairing_response_count());
  EXPECT_EQ(0, pairing_confirm_count());
  EXPECT_EQ(0, pairing_random_count());
  EXPECT_EQ(1, pairing_failed_count());
  EXPECT_EQ(0, pairing_callback_count());
  EXPECT_EQ(ErrorCode::kUnspecifiedReason, received_error_code());
}

TEST_F(SMP_SlavePairingTest, LegacyPhase2MasterConfirmValueInvalid) {
  ReceivePairingRequest();
  RunLoopUntilIdle();

  // We should have sent a pairing response and should now be in Phase 2,
  // waiting for the peer to send us Mconfirm.
  EXPECT_EQ(0, pairing_request_count());
  EXPECT_EQ(1, pairing_response_count());
  EXPECT_EQ(0, pairing_confirm_count());
  EXPECT_EQ(0, pairing_random_count());
  EXPECT_EQ(0, pairing_failed_count());
  EXPECT_EQ(0, pairing_callback_count());

  // Set up values that don't match.
  UInt128 confirm, random;
  confirm.fill(0);
  random.fill(1);

  ReceivePairingConfirm(confirm);
  RunLoopUntilIdle();

  // Slave should have sent Sconfirm.
  EXPECT_EQ(0, pairing_request_count());
  EXPECT_EQ(1, pairing_response_count());
  EXPECT_EQ(1, pairing_confirm_count());
  EXPECT_EQ(0, pairing_random_count());
  EXPECT_EQ(0, pairing_failed_count());
  EXPECT_EQ(0, pairing_callback_count());

  // Master sends Mrand that doesn't match. Slave should reject the pairing
  // without sending Srand.
  ReceivePairingRandom(random);
  RunLoopUntilIdle();
  EXPECT_EQ(0, pairing_request_count());
  EXPECT_EQ(1, pairing_response_count());
  EXPECT_EQ(1, pairing_confirm_count());
  EXPECT_EQ(0, pairing_random_count());
  EXPECT_EQ(1, pairing_failed_count());
  EXPECT_EQ(0, pairing_callback_count());
  EXPECT_EQ(ErrorCode::kConfirmValueFailed, received_error_code());
}

TEST_F(SMP_SlavePairingTest, LegacyPhase2ConfirmValuesExchanged) {
  ReceivePairingRequest();
  RunLoopUntilIdle();

  // We should have sent a pairing response and should now be in Phase 2,
  // waiting for the peer to send us Mconfirm.
  EXPECT_EQ(0, pairing_request_count());
  EXPECT_EQ(1, pairing_response_count());
  EXPECT_EQ(0, pairing_confirm_count());
  EXPECT_EQ(0, pairing_random_count());
  EXPECT_EQ(0, pairing_failed_count());
  EXPECT_EQ(0, pairing_callback_count());

  // Set up Sconfirm and Srand values that match.
  UInt128 confirm, random;
  GenerateMatchingConfirmAndRandom(&confirm, &random);

  // Master sends Mconfirm.
  ReceivePairingConfirm(confirm);
  RunLoopUntilIdle();

  // Slave should have sent Sconfirm.
  EXPECT_EQ(0, pairing_request_count());
  EXPECT_EQ(1, pairing_response_count());
  EXPECT_EQ(1, pairing_confirm_count());
  EXPECT_EQ(0, pairing_random_count());
  EXPECT_EQ(0, pairing_failed_count());
  EXPECT_EQ(0, pairing_callback_count());

  // Master sends Mrand.
  ReceivePairingRandom(random);
  RunLoopUntilIdle();

  // Slave should have sent Srand.
  EXPECT_EQ(0, pairing_request_count());
  EXPECT_EQ(1, pairing_response_count());
  EXPECT_EQ(1, pairing_confirm_count());
  EXPECT_EQ(1, pairing_random_count());
  EXPECT_EQ(0, pairing_failed_count());
  EXPECT_EQ(0, pairing_callback_count());

  // Slave's Sconfirm/Srand should be correct.
  UInt128 expected_confirm;
  GenerateConfirmValue(pairing_random(), &expected_confirm,
                       true /* peer_initiator */);
  EXPECT_EQ(expected_confirm, pairing_confirm());
}

}  // namespace
}  // namespace sm
}  // namespace btlib
