// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/gap/bredr_discovery_manager.h"

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

class BrEdrDiscoveryManagerTest : public TestingBase {
 public:
  BrEdrDiscoveryManagerTest() = default;
  ~BrEdrDiscoveryManagerTest() override = default;

  void SetUp() override {
    TestingBase::SetUp();

    discovery_manager_ =
        std::make_unique<BrEdrDiscoveryManager>(transport(), &device_cache_);

    test_device()->StartCmdChannel(test_cmd_chan());
    test_device()->StartAclChannel(test_acl_chan());
  }

  void TearDown() override {
    discovery_manager_ = nullptr;
    test_device()->Stop();
    TestingBase::TearDown();
  }

 protected:
  BrEdrDiscoveryManager* discovery_manager() const {
    return discovery_manager_.get();
  }

 private:
  RemoteDeviceCache device_cache_;
  std::unique_ptr<BrEdrDiscoveryManager> discovery_manager_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BrEdrDiscoveryManagerTest);
};

using GAP_BrEdrDiscoveryManagerTest = BrEdrDiscoveryManagerTest;

// clang-format off
const auto kInquiry = common::CreateStaticByteBuffer(
  LowerBits(hci::kInquiry), UpperBits(hci::kInquiry),
  0x05, // Paramreter total size
  0x33, 0x8B, 0x9E, // GIAC
  0x30, // kInquiryMax
  0x00 // Unlimited responses
);

const auto kInquiry_rsp = common::CreateStaticByteBuffer(
  hci::kCommandStatusEventCode,
  0x04, // parameter_total_size (4 bytes)
  hci::kSuccess, 0xF0, // success, num_hci_command_packets (240)
  LowerBits(hci::kInquiry), UpperBits(hci::kInquiry) // HCI_Inquiry opcode
);

const auto kInquiry_rsperror = common::CreateStaticByteBuffer(
  hci::kCommandStatusEventCode,
  0x04, // parameter_total_size (4 bytes)
  hci::kHardwareFailure, 0xF0, // success, num_hci_command_packets (240)
  LowerBits(hci::kInquiry), UpperBits(hci::kInquiry) // HCI_Inquiry opcode
);

const auto kInquiryComplete = common::CreateStaticByteBuffer(
  hci::kInquiryCompleteEventCode,
  0x01, // parameter_total_size (1 bytes)
  hci::kSuccess
);

const auto kInquiryCompleteError = common::CreateStaticByteBuffer(
  hci::kInquiryCompleteEventCode,
  0x01, // parameter_total_size (1 bytes)
  hci::kHardwareFailure
);

const auto kInquiryResult = common::CreateStaticByteBuffer(
  hci::kInquiryResultEventCode,
  0x0F, // parameter_total_size (15 bytes)
  0x01, // num_responses
  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, // bd_addr[0]
  0x00, // page_scan_repetition_mode[0] (R0)
  0x00, // unused / reserved
  0x00, // unused / reserved
  0x00, 0x1F, 0x00, // class_of_device[0] (unspecified)
  0x00, 0x00 // clock_offset[0]
);

const auto kInqCancel = common::CreateStaticByteBuffer(
  LowerBits(hci::kInquiryCancel), UpperBits(hci::kInquiryCancel),  // opcode
  0x00                                   // parameter_total_size
);

#define COMMAND_COMPLETE_RSP(opcode)                                         \
  common::CreateStaticByteBuffer(hci::kCommandCompleteEventCode, 0x04, 0xF0, \
                                 LowerBits((opcode)), UpperBits((opcode)),   \
                                 hci::kSuccess);

const auto kInqCancelRsp = COMMAND_COMPLETE_RSP(hci::kInquiryCancel);

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

const auto kWriteScanEnableRsp = COMMAND_COMPLETE_RSP(hci::kWriteScanEnable);

#undef COMMAND_COMPLETE_RSP
// clang-format on

// Test: discovering() answers correctly

// Test: requesting discovery should start inquiry
// Test: Inquiry Results that come in when there is discovery get reported up
// correctly to the sessions
// Test: Devices discovered are reported to the cache
// Test: Inquiry Results that come in when there's no discovery happening get
// discarded.
TEST_F(GAP_BrEdrDiscoveryManagerTest, RequestDiscoveryAndDrop) {
  test_device()->QueueCommandTransaction(
      CommandTransaction(kInquiry, {&kInquiry_rsp, &kInquiryResult}));

  std::unique_ptr<BrEdrDiscoverySession> session;
  size_t devices_found = 0u;

  discovery_manager()->RequestDiscovery(
      [&session, &devices_found](auto status, auto cb_session) {
        EXPECT_TRUE(status);
        cb_session->set_result_callback(
            [&devices_found](const auto& device) { devices_found++; });
        session = std::move(cb_session);
      });

  EXPECT_FALSE(discovery_manager()->discovering());

  RunLoopUntilIdle();

  EXPECT_EQ(1u, devices_found);
  EXPECT_TRUE(discovery_manager()->discovering());

  test_device()->QueueCommandTransaction(
      CommandTransaction(kInquiry, {&kInquiry_rsp, &kInquiryResult}));

  test_device()->SendCommandChannelPacket(kInquiryComplete);

  RunLoopUntilIdle();

  EXPECT_EQ(2u, devices_found);

  // TODO(jamuraa, NET-619): test InquiryCancel when it is implemented

  session = nullptr;
  test_device()->SendCommandChannelPacket(kInquiryResult);

  RunLoopUntilIdle();

  EXPECT_EQ(2u, devices_found);
  EXPECT_FALSE(discovery_manager()->discovering());

  test_device()->SendCommandChannelPacket(kInquiryComplete);
}

// Test: requesting a second discovery should start a session without sending
// any more HCI commands.
// Test: dropping the first discovery shouldn't stop inquiry
// Test: starting two sessions at once should only start inquiry once
TEST_F(GAP_BrEdrDiscoveryManagerTest, MultipleRequests) {
  test_device()->QueueCommandTransaction(
      CommandTransaction(kInquiry, {&kInquiry_rsp, &kInquiryResult}));

  std::unique_ptr<BrEdrDiscoverySession> session1;
  size_t devices_found1 = 0u;

  discovery_manager()->RequestDiscovery(
      [&session1, &devices_found1](auto status, auto cb_session) {
        EXPECT_TRUE(status);
        cb_session->set_result_callback(
            [&devices_found1](const auto& device) { devices_found1++; });
        session1 = std::move(cb_session);
      });

  EXPECT_FALSE(discovery_manager()->discovering());

  RunLoopUntilIdle();

  EXPECT_TRUE(session1);
  EXPECT_EQ(1u, devices_found1);
  EXPECT_TRUE(discovery_manager()->discovering());

  std::unique_ptr<BrEdrDiscoverySession> session2;
  size_t devices_found2 = 0u;

  discovery_manager()->RequestDiscovery(
      [&session2, &devices_found2](auto status, auto cb_session) {
        EXPECT_TRUE(status);
        cb_session->set_result_callback(
            [&devices_found2](const auto& device) { devices_found2++; });
        session2 = std::move(cb_session);
      });

  RunLoopUntilIdle();

  EXPECT_TRUE(session2);
  EXPECT_EQ(1u, devices_found1);
  EXPECT_EQ(0u, devices_found2);
  EXPECT_TRUE(discovery_manager()->discovering());

  test_device()->SendCommandChannelPacket(kInquiryResult);

  RunLoopUntilIdle();

  EXPECT_EQ(2u, devices_found1);
  EXPECT_EQ(1u, devices_found2);

  session1 = nullptr;

  RunLoopUntilIdle();

  test_device()->SendCommandChannelPacket(kInquiryResult);

  RunLoopUntilIdle();

  EXPECT_EQ(2u, devices_found1);
  EXPECT_EQ(2u, devices_found2);

  // TODO(jamuraa, NET-619): test InquiryCancel when it is implemented

  session2 = nullptr;

  test_device()->SendCommandChannelPacket(kInquiryResult);

  RunLoopUntilIdle();

  EXPECT_EQ(2u, devices_found1);
  EXPECT_EQ(2u, devices_found2);
  EXPECT_FALSE(discovery_manager()->discovering());

  test_device()->SendCommandChannelPacket(kInquiryComplete);
}

// Test: starting a session "while" the other one is stopping a session should
// restart the session
TEST_F(GAP_BrEdrDiscoveryManagerTest, RequestDiscoveryWhileStop) {
  test_device()->QueueCommandTransaction(
      CommandTransaction(kInquiry, {&kInquiry_rsp, &kInquiryResult}));

  std::unique_ptr<BrEdrDiscoverySession> session1;
  size_t devices_found1 = 0u;

  discovery_manager()->RequestDiscovery(
      [&session1, &devices_found1](auto status, auto cb_session) {
        EXPECT_TRUE(status);
        cb_session->set_result_callback(
            [&devices_found1](const auto& device) { devices_found1++; });
        session1 = std::move(cb_session);
      });

  EXPECT_FALSE(discovery_manager()->discovering());

  RunLoopUntilIdle();

  EXPECT_TRUE(session1);
  EXPECT_EQ(1u, devices_found1);
  EXPECT_TRUE(discovery_manager()->discovering());

  std::unique_ptr<BrEdrDiscoverySession> session2;
  size_t devices_found2 = 0u;

  // TODO(jamuraa, NET-619): test InquiryCancel when it is implemented
  test_device()->QueueCommandTransaction(
      CommandTransaction(kInquiry, {&kInquiry_rsp, &kInquiryResult}));

  session1 = nullptr;
  discovery_manager()->RequestDiscovery(
      [&session2, &devices_found2](auto status, auto cb_session) {
        EXPECT_TRUE(status);
        cb_session->set_result_callback(
            [&devices_found2](const auto& device) { devices_found2++; });
        session2 = std::move(cb_session);
      });

  // We're still waiting on the previous session to complete, so we haven't
  // started tne new session yet.
  RunLoopUntilIdle();

  test_device()->SendCommandChannelPacket(kInquiryResult);
  test_device()->SendCommandChannelPacket(kInquiryComplete);

  RunLoopUntilIdle();

  EXPECT_TRUE(session2);
  EXPECT_EQ(1u, devices_found1);
  EXPECT_EQ(1u, devices_found2);
  EXPECT_TRUE(discovery_manager()->discovering());

  test_device()->SendCommandChannelPacket(kInquiryResult);

  RunLoopUntilIdle();

  EXPECT_EQ(1u, devices_found1);
  EXPECT_EQ(2u, devices_found2);

  // TODO(jamuraa, NET-619): test InquiryCancel when it is implemented
  session2 = nullptr;

  RunLoopUntilIdle();

  EXPECT_EQ(1u, devices_found1);
  EXPECT_EQ(2u, devices_found2);
}

// Test: When Inquiry Fails to start, we report this back to the requester.
TEST_F(GAP_BrEdrDiscoveryManagerTest, RequestDiscoveryError) {
  test_device()->QueueCommandTransaction(
      CommandTransaction(kInquiry, {&kInquiry_rsperror, &kInquiryResult}));

  std::unique_ptr<BrEdrDiscoverySession> session;
  size_t devices_found = 0u;

  discovery_manager()->RequestDiscovery(
      [&session, &devices_found](auto status, auto cb_session) {
        EXPECT_FALSE(status);
        EXPECT_FALSE(cb_session);
        EXPECT_EQ(common::HostError::kProtocolError, status.error());
        EXPECT_EQ(hci::kHardwareFailure, status.protocol_error());
      });

  EXPECT_FALSE(discovery_manager()->discovering());

  RunLoopUntilIdle();

  EXPECT_FALSE(discovery_manager()->discovering());
}

// Test: When inquiry complete indicates failure, we signal to the current
// sessions.
TEST_F(GAP_BrEdrDiscoveryManagerTest, ContinuingDiscoveryError) {
  test_device()->QueueCommandTransaction(
      CommandTransaction(kInquiry, {&kInquiry_rsp, &kInquiryResult}));

  std::unique_ptr<BrEdrDiscoverySession> session;
  size_t devices_found = 0u;
  bool error_callback = false;

  discovery_manager()->RequestDiscovery(
      [&session, &devices_found, &error_callback](auto status,
                                                  auto cb_session) {
        EXPECT_TRUE(status);
        cb_session->set_result_callback(
            [&devices_found](const auto& device) { devices_found++; });
        cb_session->set_error_callback(
            [&error_callback]() { error_callback = true; });
        session = std::move(cb_session);
      });

  EXPECT_FALSE(discovery_manager()->discovering());

  RunLoopUntilIdle();

  EXPECT_EQ(1u, devices_found);
  EXPECT_TRUE(discovery_manager()->discovering());

  test_device()->SendCommandChannelPacket(kInquiryCompleteError);

  RunLoopUntilIdle();

  EXPECT_TRUE(error_callback);
  EXPECT_FALSE(discovery_manager()->discovering());

  session = nullptr;

  RunLoopUntilIdle();
}

// Test: requesting discoverable works
// Test: requesting discoverable while discoverable is pending doesn't send
// any more HCI commands
TEST_F(GAP_BrEdrDiscoveryManagerTest, DiscoverableSet) {
  test_device()->QueueCommandTransaction(
      CommandTransaction(kReadScanEnable, {}));

  std::vector<std::unique_ptr<BrEdrDiscoverableSession>> sessions;
  auto session_cb = [&sessions](auto status, auto cb_session) {
    EXPECT_TRUE(status);
    sessions.emplace_back(std::move(cb_session));
  };

  discovery_manager()->RequestDiscoverable(session_cb);

  RunLoopUntilIdle();

  EXPECT_EQ(0u, sessions.size());
  EXPECT_FALSE(discovery_manager()->discoverable());

  test_device()->QueueCommandTransaction(
      CommandTransaction(kWriteScanEnableInq, {}));

  test_device()->SendCommandChannelPacket(kReadScanEnableRspNone);

  RunLoopUntilIdle();

  // Request another session while the first is pending.
  discovery_manager()->RequestDiscoverable(session_cb);

  test_device()->SendCommandChannelPacket(kWriteScanEnableRsp);

  RunLoopUntilIdle();

  EXPECT_EQ(2u, sessions.size());
  EXPECT_TRUE(discovery_manager()->discoverable());

  discovery_manager()->RequestDiscoverable(session_cb);

  EXPECT_EQ(3u, sessions.size());
  EXPECT_TRUE(discovery_manager()->discoverable());

  test_device()->QueueCommandTransaction(
      CommandTransaction(kReadScanEnable, {&kReadScanEnableRspInquiry}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(kWriteScanEnableNone, {&kWriteScanEnableRsp}));

  sessions.clear();

  RunLoopUntilIdle();

  EXPECT_FALSE(discovery_manager()->discoverable());
}

// Test: requesting discoverable while discovery is disabling leaves
// the discoverable enabled and reports success
// Test: enable/disable while page scan is enabled works.
TEST_F(GAP_BrEdrDiscoveryManagerTest, DiscoverableRequestWhileStopping) {
  test_device()->QueueCommandTransaction(
      CommandTransaction(kReadScanEnable, {&kReadScanEnableRspPage}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(kWriteScanEnableBoth, {&kWriteScanEnableRsp}));

  std::vector<std::unique_ptr<BrEdrDiscoverableSession>> sessions;
  auto session_cb = [&sessions](auto status, auto cb_session) {
    EXPECT_TRUE(status);
    sessions.emplace_back(std::move(cb_session));
  };

  discovery_manager()->RequestDiscoverable(session_cb);

  RunLoopUntilIdle();

  EXPECT_EQ(1u, sessions.size());
  EXPECT_TRUE(discovery_manager()->discoverable());

  test_device()->QueueCommandTransaction(
      CommandTransaction(kReadScanEnable, {}));

  sessions.clear();

  RunLoopUntilIdle();

  // Request a new discovery before the procedure finishes.
  // This will queue another ReadScanEnable just in case the disable write is
  // in progress.
  test_device()->QueueCommandTransaction(
      CommandTransaction(kReadScanEnable, {}));
  discovery_manager()->RequestDiscoverable(session_cb);

  test_device()->SendCommandChannelPacket(kReadScanEnableRspBoth);

  // This shouldn't send any WriteScanEnable because we're already in the right
  // mode (TestController will assert if we do as it's not expecting)
  RunLoopUntilIdle();

  EXPECT_EQ(1u, sessions.size());
  EXPECT_TRUE(discovery_manager()->discoverable());

  // If somehow the scan got turned off, we will still turn it back on.
  test_device()->QueueCommandTransaction(
      CommandTransaction(kWriteScanEnableBoth, {&kWriteScanEnableRsp}));
  test_device()->SendCommandChannelPacket(kReadScanEnableRspPage);

  RunLoopUntilIdle();

  EXPECT_EQ(1u, sessions.size());
  EXPECT_TRUE(discovery_manager()->discoverable());

  test_device()->QueueCommandTransaction(
      CommandTransaction(kReadScanEnable, {&kReadScanEnableRspBoth}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(kWriteScanEnablePage, {&kWriteScanEnableRsp}));

  sessions.clear();

  RunLoopUntilIdle();

  EXPECT_FALSE(discovery_manager()->discoverable());
}

}  // namespace
}  // namespace gap
}  // namespace btlib
