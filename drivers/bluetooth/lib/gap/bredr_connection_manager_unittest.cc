// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/gap/bredr_connection_manager.h"

#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "garnet/drivers/bluetooth/lib/gap/remote_device_cache.h"
#include "garnet/drivers/bluetooth/lib/hci/hci.h"
#include "garnet/drivers/bluetooth/lib/testing/fake_controller_test.h"
#include "garnet/drivers/bluetooth/lib/testing/test_controller.h"

namespace btlib {
namespace gap {
namespace {

using ::btlib::testing::CommandTransaction;

using common::UpperBits;
using common::LowerBits;

using TestingBase =
    ::btlib::testing::FakeControllerTest<::btlib::testing::TestController>;

// clang-format off

const auto kReadScanEnable = common::CreateStaticByteBuffer(
    LowerBits(hci::kReadScanEnable), UpperBits(hci::kReadScanEnable),
    0x00  // No parameters
);

#define READ_SCAN_ENABLE_RSP(scan_enable)                                    \
  common::CreateStaticByteBuffer(hci::kCommandCompleteEventCode, 0x05, 0xF0, \
                                 LowerBits(hci::kReadScanEnable),            \
                                 UpperBits(hci::kReadScanEnable),            \
                                 hci::kSuccess, (scan_enable))

const auto kReadScanEnableRspNone = READ_SCAN_ENABLE_RSP(0x00);
const auto kReadScanEnableRspInquiry = READ_SCAN_ENABLE_RSP(0x01);
const auto kReadScanEnableRspPage = READ_SCAN_ENABLE_RSP(0x02);
const auto kReadScanEnableRspBoth = READ_SCAN_ENABLE_RSP(0x03);

#undef READ_SCAN_ENABLE_RSP

#define WRITE_SCAN_ENABLE_CMD(scan_enable)                               \
  common::CreateStaticByteBuffer(LowerBits(hci::kWriteScanEnable),       \
                                 UpperBits(hci::kWriteScanEnable), 0x01, \
                                 (scan_enable))

const auto kWriteScanEnableNone = WRITE_SCAN_ENABLE_CMD(0x00);
const auto kWriteScanEnableInq = WRITE_SCAN_ENABLE_CMD(0x01);
const auto kWriteScanEnablePage = WRITE_SCAN_ENABLE_CMD(0x02);
const auto kWriteScanEnableBoth = WRITE_SCAN_ENABLE_CMD(0x03);

#undef WRITE_SCAN_ENABLE_CMD

#define COMMAND_COMPLETE_RSP(opcode)                                         \
  common::CreateStaticByteBuffer(hci::kCommandCompleteEventCode, 0x04, 0xF0, \
                                 LowerBits((opcode)), UpperBits((opcode)),   \
                                 hci::kSuccess);

const auto kWriteScanEnableRsp = COMMAND_COMPLETE_RSP(hci::kWriteScanEnable);

const auto kWritePageScanActivity = common::CreateStaticByteBuffer(
    LowerBits(hci::kWritePageScanActivity),
    UpperBits(hci::kWritePageScanActivity),
    0x04,  // parameter_total_size (4 bytes)
    0x00, 0x08,  // 1.28s interval (R1)
    0x11, 0x00  // 10.625ms window (R1)
);

const auto kWritePageScanActivityRsp =
    COMMAND_COMPLETE_RSP(hci::kWritePageScanActivity);

const auto kWritePageScanType = common::CreateStaticByteBuffer(
    LowerBits(hci::kWritePageScanType), UpperBits(hci::kWritePageScanType),
    0x01,  // parameter_total_size (1 byte)
    0x01   // Interlaced scan
);

const auto kWritePageScanTypeRsp =
    COMMAND_COMPLETE_RSP(hci::kWritePageScanType);


#define COMMAND_STATUS_RSP(opcode, statuscode)                       \
  common::CreateStaticByteBuffer(hci::kCommandStatusEventCode, 0x04, \
                                 (statuscode), 0xF0,                 \
                                 LowerBits((opcode)), UpperBits((opcode)));
// clang-format on

class BrEdrConnectionManagerTest : public TestingBase {
 public:
  BrEdrConnectionManagerTest() = default;
  ~BrEdrConnectionManagerTest() override = default;

  void SetUp() override {
    TestingBase::SetUp();

    device_cache_ = std::make_unique<RemoteDeviceCache>();
    connection_manager_ = std::make_unique<BrEdrConnectionManager>(
        transport(), device_cache_.get(), true);

    test_device()->StartCmdChannel(test_cmd_chan());
    test_device()->StartAclChannel(test_acl_chan());
  }

  void TearDown() override {
    if (connection_manager_ != nullptr) {
      // deallocating the connection manager disables connectivity.
      test_device()->QueueCommandTransaction(
          CommandTransaction(kReadScanEnable, {&kReadScanEnableRspBoth}));
      test_device()->QueueCommandTransaction(
          CommandTransaction(kWriteScanEnableInq, {&kWriteScanEnableRsp}));
      connection_manager_ = nullptr;
    }
    RunLoopUntilIdle();
    test_device()->Stop();
    device_cache_ = nullptr;
    TestingBase::TearDown();
  }

 protected:
  BrEdrConnectionManager* connmgr() const { return connection_manager_.get(); }
  void SetConnectionManager(std::unique_ptr<BrEdrConnectionManager> mgr) {
    connection_manager_ = std::move(mgr);
  }

 private:
  std::unique_ptr<BrEdrConnectionManager> connection_manager_;
  std::unique_ptr<RemoteDeviceCache> device_cache_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BrEdrConnectionManagerTest);
};

using GAP_BrEdrConnectionManagerTest = BrEdrConnectionManagerTest;

TEST_F(GAP_BrEdrConnectionManagerTest, DisableConnectivity) {
  size_t cb_count = 0;
  auto cb = [&cb_count](const auto& status) {
    cb_count++;
    EXPECT_TRUE(status);
  };

  test_device()->QueueCommandTransaction(
      CommandTransaction(kReadScanEnable, {&kReadScanEnableRspPage}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(kWriteScanEnableNone, {&kWriteScanEnableRsp}));

  connmgr()->SetConnectable(false, cb);

  RunLoopUntilIdle();

  EXPECT_EQ(1u, cb_count);

  test_device()->QueueCommandTransaction(
      CommandTransaction(kReadScanEnable, {&kReadScanEnableRspBoth}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(kWriteScanEnableInq, {&kWriteScanEnableRsp}));

  connmgr()->SetConnectable(false, cb);

  RunLoopUntilIdle();

  EXPECT_EQ(2u, cb_count);
}

TEST_F(GAP_BrEdrConnectionManagerTest, EnableConnectivity) {
  size_t cb_count = 0;
  auto cb = [&cb_count](const auto& status) {
    cb_count++;
    EXPECT_TRUE(status);
  };

  test_device()->QueueCommandTransaction(
      CommandTransaction(kWritePageScanActivity, {&kWritePageScanActivityRsp}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(kWritePageScanType, {&kWritePageScanTypeRsp}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(kReadScanEnable, {&kReadScanEnableRspNone}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(kWriteScanEnablePage, {&kWriteScanEnableRsp}));

  connmgr()->SetConnectable(true, cb);

  RunLoopUntilIdle();

  EXPECT_EQ(1u, cb_count);

  test_device()->QueueCommandTransaction(
      CommandTransaction(kWritePageScanActivity, {&kWritePageScanActivityRsp}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(kWritePageScanType, {&kWritePageScanTypeRsp}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(kReadScanEnable, {&kReadScanEnableRspInquiry}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(kWriteScanEnableBoth, {&kWriteScanEnableRsp}));

  connmgr()->SetConnectable(true, cb);

  RunLoopUntilIdle();

  EXPECT_EQ(2u, cb_count);
}

const auto kConnectionRequest = common::CreateStaticByteBuffer(
    hci::kConnectionRequestEventCode,
    0x0A,  // parameter_total_size (10 byte payload)
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00,  // BD_ADDR (00:00:00:00:00:01)
    0x00, 0x1F, 0x00,                    // class_of_device (unspecified)
    0x01                                 // link_type (ACL)
);
const auto kAcceptConnectionRequest = common::CreateStaticByteBuffer(
    LowerBits(hci::kAcceptConnectionRequest),
    UpperBits(hci::kAcceptConnectionRequest),
    0x07,                                // parameter_total_size (7 bytes)
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00,  // BD_ADDR (00:00:00:00:00:01)
    0x00                                 // role (become master)
);

const auto kAcceptConnectionRequestRsp = COMMAND_STATUS_RSP(
    hci::kAcceptConnectionRequest, hci::StatusCode::kSuccess);

const auto kConnectionComplete = common::CreateStaticByteBuffer(
    hci::kConnectionCompleteEventCode,
    0x0B,                       // parameter_total_size (11 byte payload)
    hci::StatusCode::kSuccess,  // status
    0xAA, 0x0B,                 // connection_handle
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00,  // BD_ADDR (00:00:00:00:00:01)
    0x01,                                // link_type (ACL)
    0x00                                 // encryption not enabled
);
const auto kRemoteNameRequest = common::CreateStaticByteBuffer(
    LowerBits(hci::kRemoteNameRequest), UpperBits(hci::kRemoteNameRequest),
    0x0a,                                // parameter_total_size (10 bytes)
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00,  // BD_ADDR (00:00:00:00:00:01)
    0x00,                                // page_scan_repetition_mode (R0)
    0x00,                                // reserved
    0x00, 0x00                           // clock_offset
);
const auto kRemoteNameRequestRsp =
    COMMAND_STATUS_RSP(hci::kRemoteNameRequest, hci::StatusCode::kSuccess);

const auto kRemoteNameRequestComplete = common::CreateStaticByteBuffer(
    hci::kRemoteNameRequestCompleteEventCode,
    0x20,                                // parameter_total_size (32)
    hci::StatusCode::kSuccess,           // status
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00,  // BD_ADDR (00:00:00:00:00:01)
    'F', 'u', 'c', 'h', 's', 'i', 'a', 0xF0, 0x9F, 0x92, 0x96, 0x00, 0x14, 0x15,
    0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20
    // remote name (Fuchsia 💖)
    // Everything after the 0x00 should be ignored.
);
const auto kReadRemoteVersionInfo =
    common::CreateStaticByteBuffer(LowerBits(hci::kReadRemoteVersionInfo),
                                   UpperBits(hci::kReadRemoteVersionInfo),
                                   0x02,       // Parameter_total_size (2 bytes)
                                   0xAA, 0x0B  // connection_handle
    );

const auto kReadRemoteVersionInfoRsp =
    COMMAND_STATUS_RSP(hci::kReadRemoteVersionInfo, hci::StatusCode::kSuccess);

const auto kRemoteVersionInfoComplete =
    common::CreateStaticByteBuffer(hci::kReadRemoteVersionInfoCompleteEventCode,
                                   0x08,  // parameter_total_size (8 bytes)
                                   hci::StatusCode::kSuccess,  // status
                                   0xAA, 0x0B,             // connection_handle
                                   hci::HCIVersion::k4_2,  // lmp_version
                                   0xE0, 0x00,  // manufacturer_name (Google)
                                   0xAD, 0xDE   // lmp_subversion (anything)
    );

const auto kReadRemoteSupportedFeatures =
    common::CreateStaticByteBuffer(LowerBits(hci::kReadRemoteSupportedFeatures),
                                   UpperBits(hci::kReadRemoteSupportedFeatures),
                                   0x02,       // parameter_total_size (2 bytes)
                                   0xAA, 0x0B  // connection_handle
    );

const auto kReadRemoteSupportedFeaturesRsp = COMMAND_STATUS_RSP(
    hci::kReadRemoteSupportedFeatures, hci::StatusCode::kSuccess);

const auto kReadRemoteSupportedFeaturesComplete =
    common::CreateStaticByteBuffer(
        hci::kReadRemoteSupportedFeaturesCompleteEventCode,
        0x0B,                       // parameter_total_size (11 bytes)
        hci::StatusCode::kSuccess,  // status
        0xAA, 0x0B,                 // conneciton_handle,
        0xFF, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x80
        // lmp_features
        // Set: 3 slot packets, 5 slot packets, Encryption, Timing Accuracy,
        // Role Switch, Hold Mode, Sniff Mode, LE Supported, Extended Features
    );

const auto kReadRemoteExtended1 =
    common::CreateStaticByteBuffer(LowerBits(hci::kReadRemoteExtendedFeatures),
                                   UpperBits(hci::kReadRemoteExtendedFeatures),
                                   0x03,  // parameter_total_size (3 bytes)
                                   0xAA, 0x0B,  // connection_handle
                                   0x01         // page_number (1)
    );

const auto kReadRemoteExtendedFeaturesRsp = COMMAND_STATUS_RSP(
    hci::kReadRemoteExtendedFeatures, hci::StatusCode::kSuccess);

const auto kReadRemoteExtended1Complete = common::CreateStaticByteBuffer(
    hci::kReadRemoteExtendedFeaturesCompleteEventCode,
    0x0D,                       // parameter_total_size (13 bytes)
    hci::StatusCode::kSuccess,  // status
    0xAA, 0x0B,                 // connection_handle,
    0x01,                       // page_number
    0x03,                       // max_page_number (3 pages)
    0x0F, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00
    // lmp_features
    // Set: Secure Simple Pairing (Host Support), LE Supported (Host),
    //  SimultaneousLEAndBREDR, Secure Connections (Host Support)
);

const auto kReadRemoteExtended2 =
    common::CreateStaticByteBuffer(LowerBits(hci::kReadRemoteExtendedFeatures),
                                   UpperBits(hci::kReadRemoteExtendedFeatures),
                                   0x03,  // parameter_total_size (3 bytes)
                                   0xAA, 0x0B,  // connection_handle
                                   0x02         // page_number (2)
    );

const auto kReadRemoteExtended2Complete = common::CreateStaticByteBuffer(
    hci::kReadRemoteExtendedFeaturesCompleteEventCode,
    0x0D,                       // parameter_total_size (13 bytes)
    hci::StatusCode::kSuccess,  // status
    0xAA, 0x0B,                 // conneciton_handle,
    0x02,                       // page_number
    0x03,                       // max_page_number (3 pages)
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0xFF, 0x00
    // lmp_features  - All the bits should be ignored.
);

const auto kDisconnect = common::CreateStaticByteBuffer(
    LowerBits(hci::kDisconnect), UpperBits(hci::kDisconnect),
    0x03,        // parameter_total_size (3 bytes)
    0xAA, 0x0B,  // connection_handle
    0x13         // Reason (Remote User Terminated Connection)
);
const auto kDisconnectRsp =
    COMMAND_STATUS_RSP(hci::kDisconnect, hci::StatusCode::kSuccess);

const auto kDisconnectionComplete = common::CreateStaticByteBuffer(
    hci::kDisconnectionCompleteEventCode,
    0x04,                       // parameter_total_size (4 bytes)
    hci::StatusCode::kSuccess,  // status
    0xAA, 0x0B,                 // connection_handle
    0x13                        // Reason (Remote User Terminated Connection)
);

// Test: An incoming connection request should trigger an acceptance and an
// interrogation to discover capabilities.
TEST_F(GAP_BrEdrConnectionManagerTest, IncommingConnection) {
  size_t transactions = 0;
  test_device()->SetTransactionCallback([&transactions]() { transactions++; },
                                        async_get_default_dispatcher());

  test_device()->QueueCommandTransaction(
      CommandTransaction(kAcceptConnectionRequest,
                         {&kAcceptConnectionRequestRsp, &kConnectionComplete}));
  test_device()->QueueCommandTransaction(CommandTransaction(
      kRemoteNameRequest,
      {&kRemoteNameRequestRsp, &kRemoteNameRequestComplete}));
  test_device()->QueueCommandTransaction(CommandTransaction(
      kReadRemoteVersionInfo,
      {&kReadRemoteVersionInfoRsp, &kRemoteVersionInfoComplete}));
  test_device()->QueueCommandTransaction(CommandTransaction(
      kReadRemoteSupportedFeatures, {&kReadRemoteSupportedFeaturesRsp,
                                     &kReadRemoteSupportedFeaturesComplete}));
  test_device()->QueueCommandTransaction(CommandTransaction(
      kReadRemoteExtended1,
      {&kReadRemoteExtendedFeaturesRsp, &kReadRemoteExtended1Complete}));
  test_device()->QueueCommandTransaction(CommandTransaction(
      kReadRemoteExtended2,
      {&kReadRemoteExtendedFeaturesRsp, &kReadRemoteExtended2Complete}));

  test_device()->SendCommandChannelPacket(kConnectionRequest);

  RunLoopUntilIdle();

  EXPECT_EQ(6u, transactions);

  // When we deallocate the connection manager next, we should disconnect.
  test_device()->QueueCommandTransaction(CommandTransaction(
      kDisconnect, {&kDisconnectRsp, &kDisconnectionComplete}));

  // deallocating the connection manager disables connectivity.
  test_device()->QueueCommandTransaction(
      CommandTransaction(kReadScanEnable, {&kReadScanEnableRspBoth}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(kWriteScanEnableInq, {&kWriteScanEnableRsp}));

  SetConnectionManager(nullptr);

  RunLoopUntilIdle();

  EXPECT_EQ(9u, transactions);
}

const auto kRemoteNameRequestCompleteFailed =
    common::CreateStaticByteBuffer(hci::kRemoteNameRequestCompleteEventCode,
                                   0x01,  // parameter_total_size (1 bytes)
                                   hci::StatusCode::kHardwareFailure);

const auto kReadRemoteSupportedFeaturesCompleteFailed =
    common::CreateStaticByteBuffer(hci::kRemoteNameRequestCompleteEventCode,
                                   0x01,  // parameter_total_size (1 bytes)
                                   hci::StatusCode::kHardwareFailure);

// Test: if the interrogation fails, we disconnect.
//  - Receiving extra responses after a command fails will not fail
//  - We don't query extended features if we don't receive an answer.
TEST_F(GAP_BrEdrConnectionManagerTest, IncommingConnectionFailedInterrogation) {
  size_t transactions = 0;
  test_device()->SetTransactionCallback([&transactions]() { transactions++; },
                                        async_get_default_dispatcher());

  test_device()->QueueCommandTransaction(
      CommandTransaction(kAcceptConnectionRequest,
                         {&kAcceptConnectionRequestRsp, &kConnectionComplete}));
  test_device()->QueueCommandTransaction(CommandTransaction(
      kRemoteNameRequest,
      {&kRemoteNameRequestRsp, &kRemoteNameRequestCompleteFailed}));
  test_device()->QueueCommandTransaction(CommandTransaction(
      kReadRemoteVersionInfo,
      {&kReadRemoteVersionInfoRsp, &kRemoteVersionInfoComplete}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(kReadRemoteSupportedFeatures,
                         {&kReadRemoteSupportedFeaturesRsp,
                          &kReadRemoteSupportedFeaturesCompleteFailed}));

  test_device()->QueueCommandTransaction(CommandTransaction(
      kDisconnect, {&kDisconnectRsp, &kDisconnectionComplete}));

  test_device()->SendCommandChannelPacket(kConnectionRequest);

  RunLoopUntilIdle();

  EXPECT_EQ(5u, transactions);
}

const auto kCapabilitiesRequest = common::CreateStaticByteBuffer(
    hci::kIOCapabilityRequestEventCode,
    0x06,                               // parameter_total_size (6 byte payload)
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00  // BD_ADDR (00:00:00:00:00:01)
);

const auto kCapabilitiesRequestReply = common::CreateStaticByteBuffer(
    LowerBits(hci::kIOCapabilityRequestReply),
    UpperBits(hci::kIOCapabilityRequestReply),
    0x09,                                // parameter_total_size (9 bytes)
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00,  // bd_addr (match request)
    0x03,                                // No input, No output
    0x00,                                // No OOB data present
    0x00                                 // No MITM, No Pairing
);

const auto kCapabilitiesRequestReplyRsp = common::CreateStaticByteBuffer(
    hci::kCommandCompleteEventCode, 0x0A, 0xF0,
    LowerBits(hci::kIOCapabilityRequestReply),
    UpperBits(hci::kIOCapabilityRequestReply),
    hci::kSuccess,                      // status
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00  // bd_addr
);

// Test: sends replies to Capability Requests
// TODO(jamuraa): returns correct capabilities when we have different
// requirements.
TEST_F(GAP_BrEdrConnectionManagerTest, CapabilityRequest) {
  size_t transactions = 0;

  test_device()->SetTransactionCallback([&transactions]() { transactions++; },
                                        async_get_default());

  test_device()->QueueCommandTransaction(kCapabilitiesRequestReply,
                                         {&kCapabilitiesRequestReplyRsp});

  test_device()->SendCommandChannelPacket(kCapabilitiesRequest);

  RunLoopUntilIdle();

  EXPECT_EQ(1u, transactions);
}

const auto kUserConfirmationRequest = common::CreateStaticByteBuffer(
    hci::kUserConfirmationRequestEventCode,
    0x0A,  // parameter_total_size (10 byte payload)
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00,  // BD_ADDR (00:00:00:00:00:01)
    0x00, 0x00, 0x00, 0x00               // numeric value 000000
);

const auto kConfirmationRequestReply = common::CreateStaticByteBuffer(
    LowerBits(hci::kUserConfirmationRequestReply),
    UpperBits(hci::kUserConfirmationRequestReply),
    0x06,                               // parameter_total_size (9 bytes)
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00  // bd_addr (match request)
);

const auto kConfirmationRequestReplyRsp =
    COMMAND_COMPLETE_RSP(hci::kUserConfirmationRequestReply);

// Test: sends replies to Confirmation Requests
TEST_F(GAP_BrEdrConnectionManagerTest, ConfirmationRequest) {
  size_t transactions = 0;

  test_device()->SetTransactionCallback([&transactions]() { transactions++; },
                                        async_get_default());

  test_device()->QueueCommandTransaction(kConfirmationRequestReply,
                                         {&kConfirmationRequestReplyRsp});

  test_device()->SendCommandChannelPacket(kUserConfirmationRequest);

  RunLoopUntilIdle();

  EXPECT_EQ(1u, transactions);
}

#undef COMMAND_COMPLETE_RSP
#undef COMMAND_STATUS_RSP

}  // namespace
}  // namespace gap
}  // namespace btlib
