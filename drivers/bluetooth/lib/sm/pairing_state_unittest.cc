// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pairing_state.h"

#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "garnet/drivers/bluetooth/lib/hci/fake_connection.h"
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
    DestroyPairingState();
  }

  void NewPairingState(IOCapability ioc) {
    pairing_ = std::make_unique<PairingState>(ioc);
    pairing_->set_le_ltk_callback(
        fit::bind_member(this, &SMP_PairingStateTest::OnNewLTK));
    pairing_->set_legacy_tk_delegate(
        fit::bind_member(this, &SMP_PairingStateTest::OnTKRequest));
  }

  void DestroyPairingState() { pairing_ = nullptr; }

  void RegisterLE(hci::Connection::Role role) {
    FXL_DCHECK(pairing_);

    ChannelOptions options(l2cap::kLESMPChannelId);
    fake_chan_ = CreateFakeChannel(options);
    fake_chan_->SetSendCallback(
        fit::bind_member(this, &SMP_PairingStateTest::OnDataReceived),
        dispatcher());

    fake_link_ = std::make_unique<hci::testing::FakeConnection>(
        1, hci::Connection::LinkType::kLE, role, kLocalAddr, kPeerAddr);
    pairing_->RegisterLE(fake_link_->WeakPtr(), fake_chan_);
  }

  // Called by |pairing_| when a new LTK is obtained.
  void OnNewLTK(const LTK& ltk) {
    ltk_callback_count_++;
    ltk_ = ltk;
  }

  // Called to obtain a Temporary Key during legacy pairing.
  void OnTKRequest(PairingMethod method, PairingState::TKResponse responder) {
    if (tk_delegate_) {
      tk_delegate_(method, std::move(responder));
    } else {
      responder(true /* success */, 0);
    }
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

  void ReceiveEncryptionInformation(const UInt128& ltk) {
    Receive128BitCmd(kEncryptionInformation, ltk);
  }

  void ReceiveMasterIdentification(uint64_t random, uint16_t ediv) {
    StaticByteBuffer<sizeof(Header) + sizeof(MasterIdentificationParams)>
        buffer;
    PacketWriter writer(kMasterIdentification, &buffer);
    auto* params = writer.mutable_payload<MasterIdentificationParams>();
    params->ediv = htole16(ediv);
    params->rand = htole64(random);
    fake_chan()->Receive(buffer);
  }

  void GenerateConfirmValue(const UInt128& random, UInt128* out_value,
                            bool peer_initiator = false, uint32_t tk = 0) {
    FXL_DCHECK(out_value);

    tk = htole32(tk);
    UInt128 tk128;
    tk128.fill(0);
    std::memcpy(tk128.data(), &tk, sizeof(tk));

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

    util::C1(tk128, random, *preq, *pres, *init_addr, *rsp_addr, out_value);
  }

  PairingState* pairing() const { return pairing_.get(); }
  l2cap::testing::FakeChannel* fake_chan() const { return fake_chan_.get(); }
  hci::testing::FakeConnection* fake_link() const { return fake_link_.get(); }

  int pairing_callback_count() const { return pairing_callback_count_; }
  ErrorCode received_error_code() const { return received_error_code_; }
  const Status& pairing_status() const { return pairing_status_; }
  const SecurityProperties& sec_props() const { return sec_props_; }

  int ltk_callback_count() const { return ltk_callback_count_; }
  const LTK& ltk() const { return ltk_; }

  void set_tk_delegate(PairingState::TKDelegate delegate) {
    tk_delegate_ = std::move(delegate);
  }

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

  // Number of times the LTK callback has been called and the most recent LTK
  // that it was called with.
  int ltk_callback_count_ = 0;
  LTK ltk_;

  // Callback used to notify when a call to OnTKRequest() is received.
  // OnTKRequest() will reply with 0 if a callback is not set.
  PairingState::TKDelegate tk_delegate_;

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
  std::unique_ptr<hci::testing::FakeConnection> fake_link_;
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
                                        UInt128* out_random, uint32_t tk = 0) {
    FXL_DCHECK(out_confirm);
    FXL_DCHECK(out_random);
    fxl::RandBytes(out_random->data(), out_random->size());
    GenerateConfirmValue(*out_random, out_confirm, false /* peer_initiator */,
                         tk);
  }

  // Emulate legacy pairing up until before encryption with STK. Returns the STK
  // that the master is expected to encrypt the link with in |out_stk|.
  void FastForwardToSTK(UInt128* out_stk,
                        SecurityLevel level = SecurityLevel::kEncrypted,
                        KeyDistGenField remote_keys = 0,
                        KeyDistGenField local_keys = 0) {
    UpdateSecurity(level);

    PairingRequestParams pairing_params;
    pairing_params.io_capability = IOCapability::kNoInputNoOutput;
    pairing_params.auth_req = 0;
    pairing_params.max_encryption_key_size = kMaxEncryptionKeySize;
    pairing_params.initiator_key_dist_gen = local_keys;
    pairing_params.responder_key_dist_gen = remote_keys;
    ReceivePairingFeatures(pairing_params);

    // Run the loop until the harness caches the feature exchange PDUs (preq &
    // pres) so that we can generate a valid confirm value.
    RunLoopUntilIdle();

    UInt128 sconfirm, srand;
    GenerateMatchingConfirmAndRandom(&sconfirm, &srand);
    ReceivePairingConfirm(sconfirm);
    ReceivePairingRandom(srand);
    RunLoopUntilIdle();

    EXPECT_EQ(1, pairing_confirm_count());
    EXPECT_EQ(1, pairing_random_count());
    EXPECT_EQ(0, pairing_failed_count());
    EXPECT_EQ(0, pairing_callback_count());

    FXL_DCHECK(out_stk);

    UInt128 tk;
    tk.fill(0);
    util::S1(tk, srand, pairing_random(), out_stk);
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
                                        UInt128* out_random, uint32_t tk = 0) {
    FXL_DCHECK(out_confirm);
    FXL_DCHECK(out_random);
    fxl::RandBytes(out_random->data(), out_random->size());
    GenerateConfirmValue(*out_random, out_confirm, true /* peer_initiator */,
                         tk);
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

// In Phase 2 but still waiting to receive TK.
TEST_F(SMP_MasterPairingTest, ReceiveConfirmValueWhileWaitingForTK) {
  bool tk_requested = false;
  set_tk_delegate([&](auto, auto) { tk_requested = true; });

  UpdateSecurity(SecurityLevel::kEncrypted);
  ReceivePairingFeatures();
  RunLoopUntilIdle();
  EXPECT_TRUE(tk_requested);

  UInt128 confirm;
  ReceivePairingConfirm(confirm);
  RunLoopUntilIdle();

  EXPECT_EQ(0, pairing_confirm_count());
  EXPECT_EQ(0, pairing_random_count());
  EXPECT_EQ(1, pairing_failed_count());
  EXPECT_EQ(1, pairing_callback_count());
  EXPECT_EQ(ErrorCode::kUnspecifiedReason, pairing_status().protocol_error());
}

// PairingState destroyed when TKResponse runs.
TEST_F(SMP_MasterPairingTest, PairingStateDestroyedStateWhileWaitingForTK) {
  PairingState::TKResponse respond;
  set_tk_delegate([&](auto, auto rsp) { respond = std::move(rsp); });

  UpdateSecurity(SecurityLevel::kEncrypted);
  ReceivePairingFeatures();
  RunLoopUntilIdle();
  EXPECT_TRUE(respond);

  DestroyPairingState();

  // This should proceed safely.
  respond(true, 0);
  RunLoopUntilIdle();
}

// Pairing no longer in progress when TKResponse runs.
TEST_F(SMP_MasterPairingTest, PairingAbortedWhileWaitingForTK) {
  PairingState::TKResponse respond;
  set_tk_delegate([&](auto, auto rsp) { respond = std::move(rsp); });

  UpdateSecurity(SecurityLevel::kEncrypted);
  ReceivePairingFeatures();
  RunLoopUntilIdle();
  EXPECT_TRUE(respond);

  ReceivePairingFailed(ErrorCode::kPairingNotSupported);
  RunLoopUntilIdle();
  EXPECT_EQ(1, pairing_callback_count());
  EXPECT_EQ(ErrorCode::kPairingNotSupported, pairing_status().protocol_error());

  // This should have no effect.
  respond(true, 0);
  RunLoopUntilIdle();
  EXPECT_EQ(1, pairing_request_count());
  EXPECT_EQ(0, pairing_response_count());
  EXPECT_EQ(0, pairing_confirm_count());
  EXPECT_EQ(0, pairing_random_count());
  EXPECT_EQ(0, pairing_failed_count());
  EXPECT_EQ(1, pairing_callback_count());
  EXPECT_EQ(0, ltk_callback_count());
}

// Pairing procedure stopped and restarted when TKResponse runs. The TKResponse
// does not belong to the current pairing.
TEST_F(SMP_MasterPairingTest, PairingRestartedWhileWaitingForTK) {
  PairingState::TKResponse respond;
  set_tk_delegate([&](auto, auto rsp) { respond = std::move(rsp); });

  UpdateSecurity(SecurityLevel::kEncrypted);
  ReceivePairingFeatures();
  RunLoopUntilIdle();
  EXPECT_TRUE(respond);

  // Stop pairing.
  ReceivePairingFailed(ErrorCode::kPairingNotSupported);
  RunLoopUntilIdle();
  EXPECT_EQ(1, pairing_callback_count());
  EXPECT_EQ(ErrorCode::kPairingNotSupported, pairing_status().protocol_error());

  // Reset the delegate so that |respond| doesn't get overwritten by the second
  // pairing.
  set_tk_delegate(nullptr);

  UpdateSecurity(SecurityLevel::kEncrypted);
  ReceivePairingFeatures();
  RunLoopUntilIdle();
  EXPECT_EQ(2, pairing_request_count());
  EXPECT_EQ(0, pairing_response_count());
  EXPECT_EQ(1, pairing_confirm_count());
  EXPECT_EQ(0, pairing_random_count());
  EXPECT_EQ(0, pairing_failed_count());
  EXPECT_EQ(1, pairing_callback_count());
  EXPECT_EQ(0, ltk_callback_count());

  // This should have no effect.
  respond(true, 0);
  RunLoopUntilIdle();
  EXPECT_EQ(2, pairing_request_count());
  EXPECT_EQ(0, pairing_response_count());
  EXPECT_EQ(1, pairing_confirm_count());
  EXPECT_EQ(0, pairing_random_count());
  EXPECT_EQ(0, pairing_failed_count());
  EXPECT_EQ(1, pairing_callback_count());
  EXPECT_EQ(0, ltk_callback_count());
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

// In Phase 2 but still waiting to receive TK.
TEST_F(SMP_MasterPairingTest, ReceiveRandomValueWhileWaitingForTK) {
  bool tk_requested = false;
  set_tk_delegate([&](auto, auto) { tk_requested = true; });

  UpdateSecurity(SecurityLevel::kEncrypted);
  ReceivePairingFeatures();
  RunLoopUntilIdle();
  EXPECT_TRUE(tk_requested);

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

// TK delegate rejects pairing. When pairing method is "PasskeyEntryInput", this
// should result in a "Passkey Entry Failed" error.
TEST_F(SMP_MasterPairingTest, LegacyPhase2TKDelegateRejectsPasskeyInput) {
  SetUpPairingState(IOCapability::kKeyboardOnly);

  bool tk_requested = false;
  PairingState::TKResponse respond;
  PairingMethod method = PairingMethod::kJustWorks;
  set_tk_delegate([&](auto cb_method, auto cb_rsp) {
    tk_requested = true;
    method = cb_method;
    respond = std::move(cb_rsp);
  });

  UpdateSecurity(SecurityLevel::kEncrypted);
  RunLoopUntilIdle();

  // Pick I/O capabilities and MITM flags that will result in Passkey Entry
  // pairing.
  ReceivePairingFeatures(IOCapability::kDisplayOnly, AuthReq::kMITM);
  RunLoopUntilIdle();
  ASSERT_TRUE(tk_requested);
  EXPECT_EQ(PairingMethod::kPasskeyEntryInput, method);

  // Reject pairing.
  respond(false, 0);
  RunLoopUntilIdle();

  EXPECT_EQ(0, pairing_confirm_count());
  EXPECT_EQ(0, pairing_random_count());
  EXPECT_EQ(1, pairing_failed_count());
  EXPECT_EQ(1, pairing_callback_count());
  EXPECT_EQ(ErrorCode::kPasskeyEntryFailed, pairing_status().protocol_error());
}

// TK delegate rejects pairing.
TEST_F(SMP_MasterPairingTest, LegacyPhase2TKDelegateRejectsPairing) {
  bool tk_requested = false;
  PairingState::TKResponse respond;
  PairingMethod method = PairingMethod::kPasskeyEntryDisplay;
  set_tk_delegate([&](auto cb_method, auto cb_rsp) {
    tk_requested = true;
    method = cb_method;
    respond = std::move(cb_rsp);
  });

  UpdateSecurity(SecurityLevel::kEncrypted);
  RunLoopUntilIdle();

  ReceivePairingFeatures();
  RunLoopUntilIdle();
  ASSERT_TRUE(tk_requested);
  EXPECT_EQ(PairingMethod::kJustWorks, method);

  // Reject pairing.
  respond(false, 0);
  RunLoopUntilIdle();

  EXPECT_EQ(0, pairing_confirm_count());
  EXPECT_EQ(0, pairing_random_count());
  EXPECT_EQ(1, pairing_failed_count());
  EXPECT_EQ(1, pairing_callback_count());
  EXPECT_EQ(ErrorCode::kUnspecifiedReason, pairing_status().protocol_error());
}

// The TK delegate is called with the correct pairing method and the TK is
// factored into the confirm value generation.
TEST_F(SMP_MasterPairingTest, LegacyPhase2ConfirmValuesExchangedWithUserTK) {
  constexpr uint32_t kTK = 123456;

  bool tk_requested = false;
  PairingState::TKResponse respond;
  PairingMethod method = PairingMethod::kJustWorks;
  set_tk_delegate([&](auto cb_method, auto cb_rsp) {
    tk_requested = true;
    method = cb_method;
    respond = std::move(cb_rsp);
  });

  UpdateSecurity(SecurityLevel::kEncrypted);
  RunLoopUntilIdle();

  // Pick I/O capabilities and MITM flags that will result in Passkey Entry
  // pairing.
  ReceivePairingFeatures(IOCapability::kKeyboardOnly, AuthReq::kMITM);
  RunLoopUntilIdle();
  ASSERT_TRUE(tk_requested);

  // Local is DisplayOnly and peer is KeyboardOnly. Local displays passkey.
  EXPECT_EQ(PairingMethod::kPasskeyEntryDisplay, method);

  // Send TK.
  respond(true, kTK);
  RunLoopUntilIdle();

  // Should have received Mconfirm.
  EXPECT_EQ(1, pairing_confirm_count());
  EXPECT_EQ(0, pairing_random_count());
  EXPECT_EQ(0, pairing_callback_count());

  // Receive Sconfirm and Srand values that match.
  UInt128 confirm, random;
  GenerateMatchingConfirmAndRandom(&confirm, &random, kTK);

  ReceivePairingConfirm(confirm);
  RunLoopUntilIdle();

  // Should have received Mrand.
  EXPECT_EQ(1, pairing_confirm_count());
  EXPECT_EQ(1, pairing_random_count());
  EXPECT_EQ(0, pairing_callback_count());

  // Master's Mconfirm/Mrand should be correct.
  UInt128 expected_confirm;
  GenerateConfirmValue(pairing_random(), &expected_confirm,
                       false /* peer_initiator */, kTK);
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

// Encryption with STK fails.
TEST_F(SMP_MasterPairingTest, EncryptionWithSTKFails) {
  UInt128 stk;
  FastForwardToSTK(&stk);

  ASSERT_TRUE(fake_link()->ltk());
  EXPECT_EQ(stk, fake_link()->ltk()->value());
  EXPECT_EQ(0u, fake_link()->ltk()->ediv());
  EXPECT_EQ(0u, fake_link()->ltk()->rand());

  // The host should have requested encryption.
  EXPECT_EQ(1, fake_link()->start_encryption_count());

  fake_link()->TriggerEncryptionChangeCallback(
      hci::Status(hci::StatusCode::kPinOrKeyMissing), false /* enabled */);
  RunLoopUntilIdle();

  EXPECT_EQ(1, pairing_failed_count());
  EXPECT_EQ(1, pairing_callback_count());
  EXPECT_EQ(ErrorCode::kUnspecifiedReason, pairing_status().protocol_error());
  EXPECT_EQ(ErrorCode::kUnspecifiedReason, received_error_code());
}

TEST_F(SMP_MasterPairingTest, EncryptionDisabledInPhase2) {
  UInt128 stk;
  FastForwardToSTK(&stk);

  ASSERT_TRUE(fake_link()->ltk());
  EXPECT_EQ(stk, fake_link()->ltk()->value());
  EXPECT_EQ(0u, fake_link()->ltk()->ediv());
  EXPECT_EQ(0u, fake_link()->ltk()->rand());

  // The host should have requested encryption.
  EXPECT_EQ(1, fake_link()->start_encryption_count());

  fake_link()->TriggerEncryptionChangeCallback(hci::Status(),
                                               false /* enabled */);
  RunLoopUntilIdle();

  EXPECT_EQ(1, pairing_failed_count());
  EXPECT_EQ(1, pairing_callback_count());
  EXPECT_EQ(ErrorCode::kUnspecifiedReason, pairing_status().protocol_error());
  EXPECT_EQ(ErrorCode::kUnspecifiedReason, received_error_code());
}

// Tests that the pairing procedure ends after encryption with the STK if there
// are no keys to distribute.
TEST_F(SMP_MasterPairingTest, Phase3CompleteWithoutKeyExchange) {
  UInt128 stk;
  FastForwardToSTK(&stk);

  ASSERT_TRUE(fake_link()->ltk());
  EXPECT_EQ(stk, fake_link()->ltk()->value());
  EXPECT_EQ(0u, fake_link()->ltk()->ediv());
  EXPECT_EQ(0u, fake_link()->ltk()->rand());

  // The host should have requested encryption.
  EXPECT_EQ(1, fake_link()->start_encryption_count());

  fake_link()->TriggerEncryptionChangeCallback(hci::Status(),
                                               true /* enabled */);
  RunLoopUntilIdle();

  // Pairing should succeed without notifying any keys. The pairing is
  // temporary.
  EXPECT_EQ(0, pairing_failed_count());
  EXPECT_EQ(1, pairing_callback_count());
  EXPECT_EQ(0, ltk_callback_count());
  EXPECT_TRUE(pairing_status());

  EXPECT_EQ(SecurityLevel::kEncrypted, sec_props().level());
  EXPECT_EQ(16u, sec_props().enc_key_size());
  EXPECT_FALSE(sec_props().secure_connections());

  ASSERT_TRUE(fake_link()->ltk());
}

TEST_F(SMP_MasterPairingTest, Phase3EncryptionInformationReceivedTwice) {
  UInt128 stk;
  FastForwardToSTK(&stk, SecurityLevel::kEncrypted, KeyDistGen::kEncKey);

  ASSERT_TRUE(fake_link()->ltk());
  EXPECT_EQ(1, fake_link()->start_encryption_count());
  fake_link()->TriggerEncryptionChangeCallback(hci::Status(),
                                               true /* enabled */);
  RunLoopUntilIdle();

  // Pairing should still be in progress.
  EXPECT_EQ(0, pairing_failed_count());
  EXPECT_EQ(0, pairing_callback_count());
  EXPECT_EQ(0, ltk_callback_count());

  ReceiveEncryptionInformation(UInt128());
  RunLoopUntilIdle();

  // Waiting for EDIV and Rand
  EXPECT_EQ(0, pairing_failed_count());
  EXPECT_EQ(0, pairing_callback_count());

  // Send the LTK twice. This should cause pairing to fail.
  ReceiveEncryptionInformation(UInt128());
  RunLoopUntilIdle();
  EXPECT_EQ(1, pairing_failed_count());
  EXPECT_EQ(1, pairing_callback_count());
  EXPECT_EQ(ErrorCode::kUnspecifiedReason, pairing_status().protocol_error());
  EXPECT_EQ(ErrorCode::kUnspecifiedReason, received_error_code());
}

// The slave sends EDIV and Rand before LTK.
TEST_F(SMP_MasterPairingTest, Phase3MasterIdentificationReceivedInWrongOrder) {
  UInt128 stk;
  FastForwardToSTK(&stk, SecurityLevel::kEncrypted, KeyDistGen::kEncKey);

  ASSERT_TRUE(fake_link()->ltk());
  EXPECT_EQ(1, fake_link()->start_encryption_count());
  fake_link()->TriggerEncryptionChangeCallback(hci::Status(),
                                               true /* enabled */);
  RunLoopUntilIdle();

  // Pairing should still be in progress.
  EXPECT_EQ(0, pairing_failed_count());
  EXPECT_EQ(0, pairing_callback_count());
  EXPECT_EQ(0, ltk_callback_count());

  // Send master identification before encryption information. This should cause
  // pairing to fail.
  ReceiveMasterIdentification(1, 2);
  RunLoopUntilIdle();

  EXPECT_EQ(1, pairing_failed_count());
  EXPECT_EQ(1, pairing_callback_count());
  EXPECT_EQ(ErrorCode::kUnspecifiedReason, pairing_status().protocol_error());
  EXPECT_EQ(ErrorCode::kUnspecifiedReason, received_error_code());
}

TEST_F(SMP_MasterPairingTest, Phase3MasterIdentificationReceivedTwice) {
  UInt128 stk;
  FastForwardToSTK(&stk, SecurityLevel::kEncrypted, KeyDistGen::kEncKey);

  ASSERT_TRUE(fake_link()->ltk());
  EXPECT_EQ(1, fake_link()->start_encryption_count());
  fake_link()->TriggerEncryptionChangeCallback(hci::Status(),
                                               true /* enabled */);
  RunLoopUntilIdle();

  // Pairing should still be in progress.
  EXPECT_EQ(0, pairing_failed_count());
  EXPECT_EQ(0, pairing_callback_count());
  EXPECT_EQ(0, ltk_callback_count());

  ReceiveEncryptionInformation(UInt128());
  ReceiveMasterIdentification(1, 2);
  ReceiveMasterIdentification(1, 2);
  RunLoopUntilIdle();

  EXPECT_EQ(1, pairing_failed_count());
  EXPECT_EQ(1, pairing_callback_count());
  EXPECT_EQ(ErrorCode::kUnspecifiedReason, pairing_status().protocol_error());
  EXPECT_EQ(ErrorCode::kUnspecifiedReason, received_error_code());
}

// Master starts encryption with LTK after receiving LTK, ediv, and rand.
// TODO(armansito): Test that the link isn't encrypted with the LTK until all
// keys are received if the remote key distribution has more than just "enc key"
// set.
TEST_F(SMP_MasterPairingTest, Phase3EncryptionWithLTKFails) {
  UInt128 stk;
  FastForwardToSTK(&stk, SecurityLevel::kEncrypted, KeyDistGen::kEncKey);

  ASSERT_TRUE(fake_link()->ltk());
  EXPECT_EQ(1, fake_link()->start_encryption_count());
  fake_link()->TriggerEncryptionChangeCallback(hci::Status(),
                                               true /* enabled */);
  RunLoopUntilIdle();

  UInt128 kLTK{{1, 2, 3, 4, 5, 6, 7, 8, 7, 6, 5, 4, 3, 2, 1, 0}};
  uint64_t kRand = 5;
  uint16_t kEDiv = 20;

  ReceiveEncryptionInformation(kLTK);
  ReceiveMasterIdentification(kRand, kEDiv);
  RunLoopUntilIdle();

  EXPECT_EQ(0, pairing_failed_count());
  EXPECT_EQ(0, pairing_callback_count());

  ASSERT_TRUE(fake_link()->ltk());
  EXPECT_EQ(kLTK, fake_link()->ltk()->value());
  EXPECT_EQ(kRand, fake_link()->ltk()->rand());
  EXPECT_EQ(kEDiv, fake_link()->ltk()->ediv());
  EXPECT_EQ(2, fake_link()->start_encryption_count());

  // Encryption fails.
  fake_link()->TriggerEncryptionChangeCallback(
      hci::Status(hci::StatusCode::kPinOrKeyMissing), false /* enabled */);
  RunLoopUntilIdle();

  EXPECT_EQ(ErrorCode::kUnspecifiedReason, pairing_status().protocol_error());
  EXPECT_EQ(ErrorCode::kUnspecifiedReason, received_error_code());
}

// Master starts encryption with LTK after receiving LTK, ediv, and rand but it
// fails.
TEST_F(SMP_MasterPairingTest, Phase3EncryptionWithLTKSucceeds) {
  UInt128 stk;
  FastForwardToSTK(&stk, SecurityLevel::kEncrypted, KeyDistGen::kEncKey);

  ASSERT_TRUE(fake_link()->ltk());
  EXPECT_EQ(1, fake_link()->start_encryption_count());
  fake_link()->TriggerEncryptionChangeCallback(hci::Status(),
                                               true /* enabled */);
  RunLoopUntilIdle();

  UInt128 kLTK{{1, 2, 3, 4, 5, 6, 7, 8, 7, 6, 5, 4, 3, 2, 1, 0}};
  uint64_t kRand = 5;
  uint16_t kEDiv = 20;

  ReceiveEncryptionInformation(kLTK);
  ReceiveMasterIdentification(kRand, kEDiv);
  RunLoopUntilIdle();

  EXPECT_EQ(0, pairing_failed_count());
  EXPECT_EQ(0, pairing_callback_count());

  ASSERT_TRUE(fake_link()->ltk());
  EXPECT_EQ(kLTK, fake_link()->ltk()->value());
  EXPECT_EQ(kRand, fake_link()->ltk()->rand());
  EXPECT_EQ(kEDiv, fake_link()->ltk()->ediv());
  EXPECT_EQ(2, fake_link()->start_encryption_count());

  // Encryption succeeds.
  fake_link()->TriggerEncryptionChangeCallback(hci::Status(),
                                               true /* enabled */);
  RunLoopUntilIdle();

  // Pairing should succeed without notifying any keys.
  EXPECT_EQ(0, pairing_failed_count());
  EXPECT_EQ(1, pairing_callback_count());
  EXPECT_TRUE(pairing_status());

  EXPECT_EQ(SecurityLevel::kEncrypted, sec_props().level());
  EXPECT_EQ(16u, sec_props().enc_key_size());
  EXPECT_FALSE(sec_props().secure_connections());

  // Should have notified the LTK.
  EXPECT_EQ(1, ltk_callback_count());
  EXPECT_EQ(sec_props(), ltk().security());
  EXPECT_EQ(kLTK, ltk().key().value());
  EXPECT_EQ(kRand, ltk().key().rand());
  EXPECT_EQ(kEDiv, ltk().key().ediv());
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

TEST_F(SMP_SlavePairingTest, ReceiveConfirmValueWhileWaitingForTK) {
  bool tk_requested = false;
  PairingState::TKResponse respond;
  set_tk_delegate([&](auto, auto cb) {
    tk_requested = true;
    respond = std::move(cb);
  });

  ReceivePairingRequest();
  RunLoopUntilIdle();
  ASSERT_TRUE(tk_requested);

  UInt128 confirm;
  ReceivePairingConfirm(confirm);
  RunLoopUntilIdle();

  // Pairing should still be in progress without sending out any packets.
  EXPECT_EQ(0, pairing_confirm_count());
  EXPECT_EQ(0, pairing_random_count());
  EXPECT_EQ(0, pairing_failed_count());
  EXPECT_EQ(0, pairing_callback_count());

  // Respond with the TK. This should cause us to send Sconfirm.
  respond(true, 0);
  RunLoopUntilIdle();
  EXPECT_EQ(1, pairing_confirm_count());
  EXPECT_EQ(0, pairing_random_count());
  EXPECT_EQ(0, pairing_failed_count());
  EXPECT_EQ(0, pairing_callback_count());
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

// TODO(armansito): Add tests for Phase 3 in slave role

}  // namespace
}  // namespace sm
}  // namespace btlib
