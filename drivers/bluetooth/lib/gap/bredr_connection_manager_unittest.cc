// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/gap/bredr_connection_manager.h"

#include "garnet/drivers/bluetooth/lib/gap/remote_device_cache.h"
#include "garnet/drivers/bluetooth/lib/hci/hci.h"
#include "garnet/drivers/bluetooth/lib/testing/fake_controller_test.h"
#include "garnet/drivers/bluetooth/lib/testing/test_controller.h"

namespace btlib {
namespace gap {
namespace {

using ::btlib::testing::CommandTransaction;

using TestingBase =
    ::btlib::testing::FakeControllerTest<::btlib::testing::TestController>;

constexpr uint8_t UpperBits(const hci::OpCode opcode) {
  return opcode >> 8;
}

constexpr uint8_t LowerBits(const hci::OpCode opcode) {
  return opcode & 0x00FF;
}

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

#undef COMMAND_COMPLETE_RSP
// clang-format on

class BrEdrConnectionManagerTest : public TestingBase {
 public:
  BrEdrConnectionManagerTest() = default;
  ~BrEdrConnectionManagerTest() override = default;

  void SetUp() override {
    TestingBase::SetUp();

    connection_manager_ = std::make_unique<BrEdrConnectionManager>(
        transport(), &device_cache_, true);

    test_device()->StartCmdChannel(test_cmd_chan());
    test_device()->StartAclChannel(test_acl_chan());
  }

  void TearDown() override {
    // deallocating the connection manager disables connectivity.
    test_device()->QueueCommandTransaction(
        CommandTransaction(kReadScanEnable, {&kReadScanEnableRspBoth}));
    test_device()->QueueCommandTransaction(
        CommandTransaction(kWriteScanEnableInq, {&kWriteScanEnableRsp}));
    connection_manager_ = nullptr;
    RunUntilIdle();
    test_device()->Stop();
    TestingBase::TearDown();
  }

 protected:
  BrEdrConnectionManager* connmgr() const { return connection_manager_.get(); }

 private:
  RemoteDeviceCache device_cache_;
  std::unique_ptr<BrEdrConnectionManager> connection_manager_;

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

  RunUntilIdle();

  EXPECT_EQ(1u, cb_count);

  test_device()->QueueCommandTransaction(
      CommandTransaction(kReadScanEnable, {&kReadScanEnableRspBoth}));
  test_device()->QueueCommandTransaction(
      CommandTransaction(kWriteScanEnableInq, {&kWriteScanEnableRsp}));

  connmgr()->SetConnectable(false, cb);

  RunUntilIdle();

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

  RunUntilIdle();

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

  RunUntilIdle();

  EXPECT_EQ(2u, cb_count);
}

}  // namespace
}  // namespace gap
}  // namespace btlib
