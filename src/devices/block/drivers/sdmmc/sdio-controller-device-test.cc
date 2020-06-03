// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sdio-controller-device.h"

#include <lib/zx/vmo.h>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <hw/sdio.h>
#include <zxtest/zxtest.h>

#include "fake-sdmmc-device.h"

namespace {

constexpr uint32_t OpCondFunctions(uint32_t functions) {
  return SDIO_SEND_OP_COND_RESP_IORDY | (functions << SDIO_SEND_OP_COND_RESP_NUM_FUNC_LOC);
}

}  // namespace

namespace sdmmc {

class SdioControllerDeviceTest : public zxtest::Test {
 public:
  SdioControllerDeviceTest() : dut_(fake_ddk::kFakeParent, SdmmcDevice(sdmmc_.GetClient())) {}

  void SetUp() override { sdmmc_.Reset(); }

 protected:
  FakeSdmmcDevice sdmmc_;
  SdioControllerDevice dut_;
};

TEST_F(SdioControllerDeviceTest, MultiplexInterrupts) {
  EXPECT_OK(dut_.StartSdioIrqThread());
  fbl::AutoCall stop_thread([&]() { dut_.StopSdioIrqThread(); });

  zx::port port;
  ASSERT_OK(zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port));

  zx::interrupt interrupt1, interrupt2, interrupt4, interrupt7;
  ASSERT_OK(dut_.SdioGetInBandIntr(1, &interrupt1));
  ASSERT_OK(dut_.SdioGetInBandIntr(2, &interrupt2));
  ASSERT_OK(dut_.SdioGetInBandIntr(4, &interrupt4));
  ASSERT_OK(dut_.SdioGetInBandIntr(7, &interrupt7));

  ASSERT_OK(interrupt1.bind(port, 1, 0));
  ASSERT_OK(interrupt2.bind(port, 2, 0));
  ASSERT_OK(interrupt4.bind(port, 4, 0));
  ASSERT_OK(interrupt7.bind(port, 7, 0));

  sdmmc_.Write(SDIO_CIA_CCCR_INTx_INTR_PEN_ADDR, std::vector<uint8_t>{0b0000'0010}, 0);
  sdmmc_.TriggerInBandInterrupt();

  zx_port_packet_t packet;
  EXPECT_OK(port.wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 1);
  EXPECT_OK(interrupt1.ack());

  sdmmc_.Write(SDIO_CIA_CCCR_INTx_INTR_PEN_ADDR, std::vector<uint8_t>{0b1111'1110}, 0);
  sdmmc_.TriggerInBandInterrupt();

  EXPECT_OK(port.wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 1);
  EXPECT_OK(interrupt1.ack());

  EXPECT_OK(port.wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 2);
  EXPECT_OK(interrupt2.ack());

  EXPECT_OK(port.wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 4);
  EXPECT_OK(interrupt4.ack());

  EXPECT_OK(port.wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 7);
  EXPECT_OK(interrupt7.ack());

  sdmmc_.Write(SDIO_CIA_CCCR_INTx_INTR_PEN_ADDR, std::vector<uint8_t>{0b1010'0010}, 0);
  sdmmc_.TriggerInBandInterrupt();

  EXPECT_OK(port.wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 1);
  EXPECT_OK(interrupt1.ack());

  EXPECT_OK(port.wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 7);
  EXPECT_OK(interrupt7.ack());

  sdmmc_.Write(SDIO_CIA_CCCR_INTx_INTR_PEN_ADDR, std::vector<uint8_t>{0b0011'0110}, 0);
  sdmmc_.TriggerInBandInterrupt();

  EXPECT_OK(port.wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 1);
  EXPECT_OK(interrupt1.ack());

  EXPECT_OK(port.wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 2);
  EXPECT_OK(interrupt2.ack());

  EXPECT_OK(port.wait(zx::time::infinite(), &packet));
  EXPECT_EQ(packet.key, 4);
  EXPECT_OK(interrupt4.ack());
}

TEST_F(SdioControllerDeviceTest, SdioDoRwTxn) {
  // Report five IO functions.
  sdmmc_.set_command_callback(
      SDIO_SEND_OP_COND, [](sdmmc_req_t* req) -> void { req->response[0] = OpCondFunctions(5); });
  sdmmc_.Write(SDIO_CIA_CCCR_CARD_CAPS_ADDR, std::vector<uint8_t>{0x00}, 0);

  // Set the maximum block size for function three to eight bytes.
  sdmmc_.Write(0x0309, std::vector<uint8_t>{0x00, 0x10, 0x00}, 0);
  sdmmc_.Write(0x1000, std::vector<uint8_t>{0x22, 0x2a, 0x01}, 0);
  sdmmc_.Write(0x100e, std::vector<uint8_t>{0x08, 0x00}, 0);

  sdmmc_.set_host_info({
      .caps = 0,
      .max_transfer_size = 16,
      .max_transfer_size_non_dma = 16,
      .prefs = 0,
  });
  EXPECT_OK(dut_.Init());

  EXPECT_OK(dut_.ProbeSdio());
  EXPECT_OK(dut_.SdioUpdateBlockSize(3, 0, true));

  uint16_t block_size = 0;
  EXPECT_OK(dut_.SdioGetBlockSize(3, &block_size));
  EXPECT_EQ(block_size, 8);

  constexpr uint8_t kTestData[52] = {
      0xff, 0x7c, 0xa6, 0x24, 0x6f, 0x69, 0x7a, 0x39, 0x63, 0x68, 0xef, 0x28, 0xf3,
      0x18, 0x91, 0xf1, 0x68, 0x48, 0x78, 0x2f, 0xbb, 0xb2, 0x9a, 0x63, 0x51, 0xd4,
      0xe1, 0x94, 0xb4, 0x5c, 0x81, 0x94, 0xc7, 0x86, 0x50, 0x33, 0x61, 0xf8, 0x97,
      0x4c, 0x68, 0x71, 0x7f, 0x17, 0x59, 0x82, 0xc5, 0x36, 0xe0, 0x20, 0x0b, 0x56,
  };

  uint8_t buffer[sizeof(kTestData)];

  memcpy(buffer, kTestData, sizeof(buffer));
  sdio_rw_txn_t txn = {
      .addr = 0x1ab08,
      .data_size = 36,
      .incr = false,
      .write = true,
      .use_dma = false,
      .dma_vmo = ZX_HANDLE_INVALID,
      .virt_buffer = buffer,
      .virt_size = 0,
      .buf_offset = 16,
  };
  EXPECT_OK(dut_.SdioDoRwTxn(3, &txn));

  // The write sequence should be: four writes of blocks of eight, one write of four bytes. This is
  // a FIFO write, meaning the data will get overwritten each time. Verify the final state of the
  // device.
  const std::vector<uint8_t> read_data = sdmmc_.Read(0x1ab08, 16, 3);
  EXPECT_BYTES_EQ(read_data.data(), buffer + sizeof(buffer) - 4, 4);
  EXPECT_BYTES_EQ(read_data.data() + 4, buffer + sizeof(buffer) - 8, 4);

  sdmmc_.Write(0x12308, fbl::Span<const uint8_t>(kTestData, sizeof(kTestData)), 3);
  memset(buffer, 0, sizeof(buffer));
  txn = {
      .addr = 0x12308,
      .data_size = 36,
      .incr = true,
      .write = false,
      .use_dma = false,
      .dma_vmo = ZX_HANDLE_INVALID,
      .virt_buffer = buffer,
      .virt_size = 0,
      .buf_offset = 16,
  };
  EXPECT_OK(dut_.SdioDoRwTxn(3, &txn));

  EXPECT_BYTES_EQ(buffer + 16, kTestData, 36);
}

TEST_F(SdioControllerDeviceTest, SdioDoRwTxnMultiBlock) {
  sdmmc_.set_command_callback(
      SDIO_SEND_OP_COND, [](sdmmc_req_t* req) -> void { req->response[0] = OpCondFunctions(7); });

  sdmmc_.Write(SDIO_CIA_CCCR_CARD_CAPS_ADDR, std::vector<uint8_t>{SDIO_CIA_CCCR_CARD_CAP_SMB}, 0);

  // Set the maximum block size for function seven to eight bytes.
  sdmmc_.Write(0x709, std::vector<uint8_t>{0x00, 0x10, 0x00}, 0);
  sdmmc_.Write(0x1000, std::vector<uint8_t>{0x22, 0x2a, 0x01}, 0);
  sdmmc_.Write(0x100e, std::vector<uint8_t>{0x08, 0x00}, 0);

  sdmmc_.set_host_info({
      .caps = 0,
      .max_transfer_size = 32,
      .max_transfer_size_non_dma = 32,
      .prefs = 0,
  });
  EXPECT_OK(dut_.Init());

  EXPECT_OK(dut_.ProbeSdio());
  EXPECT_OK(dut_.SdioUpdateBlockSize(7, 0, true));

  uint16_t block_size = 0;
  EXPECT_OK(dut_.SdioGetBlockSize(7, &block_size));
  EXPECT_EQ(block_size, 8);

  constexpr uint8_t kTestData[132] = {
      // clang-format off
      0x94, 0xfa, 0x41, 0x93, 0x40, 0x81, 0xae, 0x83, 0x85, 0x88, 0x98, 0x6d,
      0x52, 0x1c, 0x53, 0x9c, 0xa7, 0x7a, 0x19, 0x74, 0xc9, 0xa9, 0x47, 0xd9,
      0x64, 0x2b, 0x76, 0x47, 0x55, 0x0b, 0x3d, 0x34, 0xd6, 0xfc, 0xca, 0x7b,
      0xae, 0xe0, 0xff, 0xe3, 0xa2, 0xd3, 0xe5, 0xb6, 0xbc, 0xa4, 0x3d, 0x01,
      0x99, 0x92, 0xdc, 0xac, 0x68, 0xb1, 0x88, 0x22, 0xc4, 0xf4, 0x1a, 0x45,
      0xe9, 0xd3, 0x5e, 0x8c, 0x24, 0x98, 0x7b, 0xf5, 0x32, 0x6d, 0xe5, 0x01,
      0x36, 0x03, 0x9b, 0xee, 0xfa, 0x23, 0x2f, 0xdd, 0xc6, 0xa4, 0x34, 0x58,
      0x23, 0xaa, 0xc9, 0x00, 0x73, 0xb8, 0xe0, 0xd8, 0xde, 0xc4, 0x59, 0x66,
      0x76, 0xd3, 0x65, 0xe0, 0xfa, 0xf7, 0x89, 0x40, 0x3a, 0xa8, 0x83, 0x53,
      0x63, 0xf4, 0x36, 0xea, 0xb3, 0x94, 0xe7, 0x5f, 0x3c, 0xed, 0x8d, 0x3e,
      0xee, 0x1b, 0x75, 0xea, 0xb3, 0x95, 0xd2, 0x25, 0x7c, 0xb9, 0x6d, 0x37,
      // clang-format on
  };

  uint8_t buffer[sizeof(kTestData)] = {};

  sdmmc_.Write(0x1ab08, fbl::Span<const uint8_t>(kTestData, sizeof(kTestData)), 7);
  sdio_rw_txn_t txn = {
      .addr = 0x1ab08,
      .data_size = 68,
      .incr = false,
      .write = false,
      .use_dma = false,
      .dma_vmo = ZX_HANDLE_INVALID,
      .virt_buffer = buffer,
      .virt_size = 0,
      .buf_offset = 64,
  };
  EXPECT_OK(dut_.SdioDoRwTxn(7, &txn));

  EXPECT_BYTES_EQ(buffer + 64, kTestData, 32);
  EXPECT_BYTES_EQ(buffer + 96, kTestData, 32);
  EXPECT_BYTES_EQ(buffer + 128, kTestData, 4);

  memcpy(buffer, kTestData, sizeof(buffer));
  txn = {
      .addr = 0x12308,
      .data_size = 68,
      .incr = true,
      .write = true,
      .use_dma = false,
      .dma_vmo = ZX_HANDLE_INVALID,
      .virt_buffer = buffer,
      .virt_size = 0,
      .buf_offset = 64,
  };
  EXPECT_OK(dut_.SdioDoRwTxn(7, &txn));

  EXPECT_BYTES_EQ(sdmmc_.Read(0x12308, 68, 7).data(), kTestData + 64, 68);
}

TEST_F(SdioControllerDeviceTest, DdkLifecycle) {
  // The interrupt thread is started by AddDevice.
  fbl::AutoCall stop_thread([&]() { dut_.StopSdioIrqThread(); });

  sdmmc_.set_command_callback(
      SDIO_SEND_OP_COND, [](sdmmc_req_t* req) -> void { req->response[0] = OpCondFunctions(4); });

  EXPECT_OK(dut_.ProbeSdio());

  Bind ddk;
  EXPECT_OK(dut_.AddDevice());

  dut_.DdkAsyncRemove();
  ddk.Ok();
  EXPECT_EQ(ddk.total_children(), 4);
}

TEST_F(SdioControllerDeviceTest, SdioIntrPending) {
  bool pending;

  sdmmc_.Write(SDIO_CIA_CCCR_INTx_INTR_PEN_ADDR, std::vector<uint8_t>{0b0011'0010}, 0);
  EXPECT_OK(dut_.SdioIntrPending(4, &pending));
  EXPECT_TRUE(pending);

  sdmmc_.Write(SDIO_CIA_CCCR_INTx_INTR_PEN_ADDR, std::vector<uint8_t>{0b0010'0010}, 0);
  EXPECT_OK(dut_.SdioIntrPending(4, &pending));
  EXPECT_FALSE(pending);

  sdmmc_.Write(SDIO_CIA_CCCR_INTx_INTR_PEN_ADDR, std::vector<uint8_t>{0b1000'0000}, 0);
  EXPECT_OK(dut_.SdioIntrPending(7, &pending));
  EXPECT_TRUE(pending);

  sdmmc_.Write(SDIO_CIA_CCCR_INTx_INTR_PEN_ADDR, std::vector<uint8_t>{0b0000'0000}, 0);
  EXPECT_OK(dut_.SdioIntrPending(7, &pending));
  EXPECT_FALSE(pending);

  sdmmc_.Write(SDIO_CIA_CCCR_INTx_INTR_PEN_ADDR, std::vector<uint8_t>{0b0000'1110}, 0);
  EXPECT_OK(dut_.SdioIntrPending(1, &pending));
  EXPECT_TRUE(pending);

  sdmmc_.Write(SDIO_CIA_CCCR_INTx_INTR_PEN_ADDR, std::vector<uint8_t>{0b0000'1110}, 0);
  EXPECT_OK(dut_.SdioIntrPending(2, &pending));
  EXPECT_TRUE(pending);

  sdmmc_.Write(SDIO_CIA_CCCR_INTx_INTR_PEN_ADDR, std::vector<uint8_t>{0b0000'1110}, 0);
  EXPECT_OK(dut_.SdioIntrPending(3, &pending));
  EXPECT_TRUE(pending);
}

TEST_F(SdioControllerDeviceTest, EnableDisableFnIntr) {
  sdmmc_.Write(0x04, std::vector<uint8_t>{0b0000'0000}, 0);

  EXPECT_OK(dut_.SdioEnableFnIntr(4));
  EXPECT_EQ(sdmmc_.Read(0x04, 1, 0)[0], 0b0001'0001);

  EXPECT_OK(dut_.SdioEnableFnIntr(7));
  EXPECT_EQ(sdmmc_.Read(0x04, 1, 0)[0], 0b1001'0001);

  EXPECT_OK(dut_.SdioEnableFnIntr(4));
  EXPECT_EQ(sdmmc_.Read(0x04, 1, 0)[0], 0b1001'0001);

  EXPECT_OK(dut_.SdioDisableFnIntr(4));
  EXPECT_EQ(sdmmc_.Read(0x04, 1, 0)[0], 0b1000'0001);

  EXPECT_OK(dut_.SdioDisableFnIntr(7));
  EXPECT_EQ(sdmmc_.Read(0x04, 1, 0)[0], 0b0000'0000);

  EXPECT_NOT_OK(dut_.SdioDisableFnIntr(7));
}

TEST_F(SdioControllerDeviceTest, ProcessCccr) {
  sdmmc_.set_command_callback(
      SDIO_SEND_OP_COND, [](sdmmc_req_t* req) -> void { req->response[0] = OpCondFunctions(0); });

  sdmmc_.Write(0x00, std::vector<uint8_t>{0x43}, 0);  // CCCR/SDIO revision.
  sdmmc_.Write(0x08, std::vector<uint8_t>{0xc2}, 0);  // Card capability.
  sdmmc_.Write(0x13, std::vector<uint8_t>{0xa9}, 0);  // Bus speed select.
  sdmmc_.Write(0x14, std::vector<uint8_t>{0x3f}, 0);  // UHS-I support.
  sdmmc_.Write(0x15, std::vector<uint8_t>{0xb7}, 0);  // Driver strength.

  EXPECT_OK(dut_.ProbeSdio());
  sdio_hw_info_t info = {};
  EXPECT_OK(dut_.SdioGetDevHwInfo(&info));
  EXPECT_EQ(info.dev_hw_info.caps,
            SDIO_CARD_MULTI_BLOCK | SDIO_CARD_LOW_SPEED | SDIO_CARD_FOUR_BIT_BUS |
                SDIO_CARD_HIGH_SPEED | SDIO_CARD_UHS_SDR50 | SDIO_CARD_UHS_SDR104 |
                SDIO_CARD_UHS_DDR50 | SDIO_CARD_TYPE_A | SDIO_CARD_TYPE_B | SDIO_CARD_TYPE_D);

  sdmmc_.Write(0x08, std::vector<uint8_t>{0x00}, 0);
  sdmmc_.Write(0x13, std::vector<uint8_t>{0x00}, 0);
  sdmmc_.Write(0x14, std::vector<uint8_t>{0x00}, 0);
  sdmmc_.Write(0x15, std::vector<uint8_t>{0x00}, 0);

  EXPECT_OK(dut_.ProbeSdio());
  EXPECT_OK(dut_.SdioGetDevHwInfo(&info));
  EXPECT_EQ(info.dev_hw_info.caps, 0);

  sdmmc_.Write(0x00, std::vector<uint8_t>{0x41}, 0);
  EXPECT_NOT_OK(dut_.ProbeSdio());

  sdmmc_.Write(0x00, std::vector<uint8_t>{0x33}, 0);
  EXPECT_NOT_OK(dut_.ProbeSdio());
}

TEST_F(SdioControllerDeviceTest, ProcessCis) {
  sdmmc_.set_command_callback(
      SDIO_SEND_OP_COND, [](sdmmc_req_t* req) -> void { req->response[0] = OpCondFunctions(5); });

  sdmmc_.Write(0x0000'0509, std::vector<uint8_t>{0xa2, 0xc2, 0x00}, 0);  // CIS pointer.

  sdmmc_.Write(0x0000'c2a2,
               std::vector<uint8_t>{
                   0x20,        // Manufacturer ID tuple.
                   0x04,        // Manufacturer ID tuple size.
                   0x01, 0xc0,  // Manufacturer code.
                   0xce, 0xfa,  // Manufacturer information (part number/revision).
                   0x00,        // Null tuple.
                   0x22,        // Function extensions tuple.
                   0x2a,        // Function extensions tuple size.
                   0x01,        // Type of extended data.
               },
               0);
  sdmmc_.Write(0x0000'c2b7, std::vector<uint8_t>{0x00, 0x01}, 0);  // Function block size.
  sdmmc_.Write(0x0000'c2d5, std::vector<uint8_t>{0x00}, 0);        // End-of-chain tuple.

  EXPECT_OK(dut_.ProbeSdio());

  sdio_hw_info_t info = {};
  EXPECT_OK(dut_.SdioGetDevHwInfo(&info));

  EXPECT_EQ(info.dev_hw_info.num_funcs, 6);
  EXPECT_EQ(info.funcs_hw_info[5].max_blk_size, 256);
  EXPECT_EQ(info.funcs_hw_info[5].manufacturer_id, 0xc001);
  EXPECT_EQ(info.funcs_hw_info[5].product_id, 0xface);
}

TEST_F(SdioControllerDeviceTest, ProcessCisFunction0) {
  sdmmc_.set_command_callback(
      SDIO_SEND_OP_COND, [](sdmmc_req_t* req) -> void { req->response[0] = OpCondFunctions(5); });

  sdmmc_.set_host_info({
      .caps = 0,
      .max_transfer_size = 1024,
      .max_transfer_size_non_dma = 1024,
      .prefs = 0,
  });
  EXPECT_OK(dut_.Init());

  sdmmc_.Write(0x0000'0009, std::vector<uint8_t>{0xf5, 0x61, 0x01}, 0);  // CIS pointer.

  sdmmc_.Write(0x0001'61f5,
               std::vector<uint8_t>{
                   0x22,        // Function extensions tuple.
                   0x04,        // Function extensions tuple size.
                   0x00,        // Type of extended data.
                   0x00, 0x02,  // Function 0 block size.
                   0x32,        // Max transfer speed.
                   0x00,        // Null tuple.
                   0x20,        // Manufacturer ID tuple.
                   0x04,        // Manufacturer ID tuple size.
                   0xef, 0xbe,  // Manufacturer code.
                   0xfe, 0xca,  // Manufacturer information (part number/revision).
                   0xff,        // End-of-chain tuple.
               },
               0);

  EXPECT_OK(dut_.ProbeSdio());

  sdio_hw_info_t info = {};
  EXPECT_OK(dut_.SdioGetDevHwInfo(&info));

  EXPECT_EQ(info.dev_hw_info.num_funcs, 6);
  EXPECT_EQ(info.funcs_hw_info[0].max_blk_size, 512);
  EXPECT_EQ(info.funcs_hw_info[0].max_tran_speed, 25000);
  EXPECT_EQ(info.funcs_hw_info[0].manufacturer_id, 0xbeef);
  EXPECT_EQ(info.funcs_hw_info[0].product_id, 0xcafe);
}

TEST_F(SdioControllerDeviceTest, ProcessFbr) {
  sdmmc_.set_command_callback(
      SDIO_SEND_OP_COND, [](sdmmc_req_t* req) -> void { req->response[0] = OpCondFunctions(7); });

  sdmmc_.Write(0x100, std::vector<uint8_t>{0x83}, 0);
  sdmmc_.Write(0x500, std::vector<uint8_t>{0x00}, 0);
  sdmmc_.Write(0x600, std::vector<uint8_t>{0xcf}, 0);
  sdmmc_.Write(0x601, std::vector<uint8_t>{0xab}, 0);
  sdmmc_.Write(0x700, std::vector<uint8_t>{0x4e}, 0);

  EXPECT_OK(dut_.ProbeSdio());

  sdio_hw_info_t info = {};
  EXPECT_OK(dut_.SdioGetDevHwInfo(&info));

  EXPECT_EQ(info.dev_hw_info.num_funcs, 8);
  EXPECT_EQ(info.funcs_hw_info[1].fn_intf_code, 0x03);
  EXPECT_EQ(info.funcs_hw_info[5].fn_intf_code, 0x00);
  EXPECT_EQ(info.funcs_hw_info[6].fn_intf_code, 0xab);
  EXPECT_EQ(info.funcs_hw_info[7].fn_intf_code, 0x0e);
}

TEST_F(SdioControllerDeviceTest, SmallHostTransferSize) {
  sdmmc_.set_command_callback(
      SDIO_SEND_OP_COND, [](sdmmc_req_t* req) -> void { req->response[0] = OpCondFunctions(3); });

  sdmmc_.set_host_info({
      .caps = 0,
      .max_transfer_size = 32,
      .max_transfer_size_non_dma = 64,
      .prefs = 0,
  });
  EXPECT_OK(dut_.Init());

  // Set the maximum block size for function three to 128 bytes.
  sdmmc_.Write(0x0309, std::vector<uint8_t>{0x00, 0x10, 0x00}, 0);
  sdmmc_.Write(0x1000, std::vector<uint8_t>{0x22, 0x2a, 0x01}, 0);
  sdmmc_.Write(0x100e, std::vector<uint8_t>{0x80, 0x00}, 0);

  EXPECT_OK(dut_.ProbeSdio());
  EXPECT_OK(dut_.SdioUpdateBlockSize(3, 0, true));

  uint16_t block_size = 0;
  EXPECT_OK(dut_.SdioGetBlockSize(3, &block_size));
  EXPECT_EQ(block_size, 128);

  constexpr uint8_t kTestData[128] = {
      // clang-format off
      0x28, 0x52, 0xe3, 0x9a, 0xa5, 0x5f, 0x39, 0x43,
      0x7b, 0xb5, 0x24, 0xe7, 0x30, 0x7b, 0x13, 0xc4,
      0x28, 0xe6, 0xd5, 0xb5, 0xf9, 0x1d, 0xd4, 0x8b,
      0x2e, 0xfb, 0xdc, 0x5e, 0x89, 0x1e, 0xef, 0x8b,
      0xa6, 0x7d, 0xf4, 0xb0, 0x87, 0x6f, 0x80, 0x48,
      0x71, 0x39, 0x4b, 0x28, 0x3d, 0xf9, 0xa7, 0xbb,
      0x8f, 0x13, 0x34, 0x2b, 0xbc, 0xd3, 0x4e, 0xbe,
      0xd1, 0x9d, 0x48, 0x1c, 0x79, 0x62, 0x72, 0x48,
      0x4b, 0xf0, 0x71, 0x1c, 0x97, 0x99, 0x4d, 0x0f,
      0x5a, 0xa1, 0xc2, 0xb5, 0xa1, 0xca, 0x89, 0x34,
      0xd9, 0x1a, 0x13, 0xfa, 0xfd, 0x76, 0x74, 0x51,
      0xfe, 0x24, 0xd1, 0xc3, 0x89, 0x53, 0x36, 0x14,
      0xbd, 0x66, 0x59, 0xba, 0xc9, 0x3b, 0x9e, 0x0f,
      0x8f, 0x6b, 0x26, 0x72, 0x72, 0x76, 0x70, 0x68,
      0xd6, 0x5f, 0x3b, 0x6e, 0x2e, 0xda, 0x51, 0xf7,
      0x55, 0x8b, 0x0e, 0xed, 0x93, 0x71, 0x48, 0xc2,
      // clang-format on
  };

  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(fbl::round_up<size_t, size_t>(sizeof(kTestData), PAGE_SIZE), 0, &vmo));
  ASSERT_OK(vmo.write(kTestData, 0, sizeof(kTestData)));

  uint8_t buffer[sizeof(kTestData)];
  memcpy(buffer, kTestData, sizeof(buffer));

  sdio_rw_txn_t txn = {
      .addr = 0,
      .data_size = 64,
      .incr = false,
      .write = true,
      .use_dma = true,
      .dma_vmo = vmo.get(),
      .virt_buffer = buffer,
      .virt_size = 0,
      .buf_offset = 0,
  };

  EXPECT_NOT_OK(dut_.SdioDoRwTxn(3, &txn));

  txn.use_dma = false;
  EXPECT_OK(dut_.SdioDoRwTxn(3, &txn));
  EXPECT_BYTES_EQ(sdmmc_.Read(0, 64, 3).data(), kTestData, 64);

  txn.data_size = 128;
  EXPECT_NOT_OK(dut_.SdioDoRwTxn(3, &txn));

  txn.use_dma = true;
  EXPECT_NOT_OK(dut_.SdioDoRwTxn(3, &txn));
}

TEST_F(SdioControllerDeviceTest, ProbeFail) {
  sdmmc_.set_command_callback(
      SDIO_SEND_OP_COND, [](sdmmc_req_t* req) -> void { req->response[0] = OpCondFunctions(5); });

  // Set the function 3 CIS pointer to zero. This should cause InitFunc and subsequently ProbeSdio
  // to fail.
  sdmmc_.Write(0x0309, std::vector<uint8_t>{0x00, 0x00, 0x00}, 0);

  EXPECT_NOT_OK(dut_.ProbeSdio());
}

TEST_F(SdioControllerDeviceTest, ProbeSdr104) {
  sdmmc_.set_command_callback(SDIO_SEND_OP_COND, [](sdmmc_req_t* req) -> void {
    req->response[0] = OpCondFunctions(5) | SDIO_SEND_OP_COND_RESP_S18A;
  });

  sdmmc_.Write(0x0014, std::vector<uint8_t>{0x07}, 0);

  sdmmc_.set_host_info({
      .caps = SDMMC_HOST_CAP_VOLTAGE_330 | SDMMC_HOST_CAP_SDR104 | SDMMC_HOST_CAP_SDR50 |
              SDMMC_HOST_CAP_DDR50,
      .max_transfer_size = 0x1000,
      .max_transfer_size_non_dma = 0x1000,
      .prefs = 0,
  });
  EXPECT_OK(dut_.Init());

  EXPECT_OK(dut_.ProbeSdio());

  EXPECT_EQ(sdmmc_.signal_voltage(), SDMMC_VOLTAGE_V180);
  EXPECT_EQ(sdmmc_.bus_width(), SDMMC_BUS_WIDTH_FOUR);
  EXPECT_EQ(sdmmc_.bus_freq(), 208'000'000);
  EXPECT_EQ(sdmmc_.timing(), SDMMC_TIMING_SDR104);
}

TEST_F(SdioControllerDeviceTest, ProbeSdr50LimitedByHost) {
  sdmmc_.set_command_callback(SDIO_SEND_OP_COND, [](sdmmc_req_t* req) -> void {
    req->response[0] = OpCondFunctions(5) | SDIO_SEND_OP_COND_RESP_S18A;
  });

  sdmmc_.Write(0x0014, std::vector<uint8_t>{0x07}, 0);

  sdmmc_.set_host_info({
      .caps = SDMMC_HOST_CAP_VOLTAGE_330 | SDMMC_HOST_CAP_SDR50,
      .max_transfer_size = 0x1000,
      .max_transfer_size_non_dma = 0x1000,
      .prefs = 0,
  });
  EXPECT_OK(dut_.Init());

  EXPECT_OK(dut_.ProbeSdio());

  EXPECT_EQ(sdmmc_.signal_voltage(), SDMMC_VOLTAGE_V180);
  EXPECT_EQ(sdmmc_.bus_width(), SDMMC_BUS_WIDTH_FOUR);
  EXPECT_EQ(sdmmc_.bus_freq(), 100'000'000);
  EXPECT_EQ(sdmmc_.timing(), SDMMC_TIMING_SDR50);
}

TEST_F(SdioControllerDeviceTest, ProbeSdr50LimitedByCard) {
  sdmmc_.set_command_callback(SDIO_SEND_OP_COND, [](sdmmc_req_t* req) -> void {
    req->response[0] = OpCondFunctions(5) | SDIO_SEND_OP_COND_RESP_S18A;
  });

  sdmmc_.Write(0x0014, std::vector<uint8_t>{0x01}, 0);

  sdmmc_.set_host_info({
      .caps = SDMMC_HOST_CAP_VOLTAGE_330 | SDMMC_HOST_CAP_SDR104 | SDMMC_HOST_CAP_SDR50 |
              SDMMC_HOST_CAP_DDR50,
      .max_transfer_size = 0x1000,
      .max_transfer_size_non_dma = 0x1000,
      .prefs = 0,
  });
  EXPECT_OK(dut_.Init());

  EXPECT_OK(dut_.ProbeSdio());

  EXPECT_EQ(sdmmc_.signal_voltage(), SDMMC_VOLTAGE_V180);
  EXPECT_EQ(sdmmc_.bus_width(), SDMMC_BUS_WIDTH_FOUR);
  EXPECT_EQ(sdmmc_.bus_freq(), 100'000'000);
  EXPECT_EQ(sdmmc_.timing(), SDMMC_TIMING_SDR50);
}

TEST_F(SdioControllerDeviceTest, ProbeFallBackToHs) {
  sdmmc_.set_command_callback(SDIO_SEND_OP_COND, [](sdmmc_req_t* req) -> void {
    req->response[0] = OpCondFunctions(5) | SDIO_SEND_OP_COND_RESP_S18A;
  });

  sdmmc_.Write(0x0008, std::vector<uint8_t>{0x00}, 0);
  sdmmc_.Write(0x0014, std::vector<uint8_t>{0x07}, 0);

  sdmmc_.set_perform_tuning_status(ZX_ERR_IO);
  sdmmc_.set_host_info({
      .caps = SDMMC_HOST_CAP_VOLTAGE_330 | SDMMC_HOST_CAP_SDR104 | SDMMC_HOST_CAP_SDR50 |
              SDMMC_HOST_CAP_DDR50,
      .max_transfer_size = 0x1000,
      .max_transfer_size_non_dma = 0x1000,
      .prefs = 0,
  });
  EXPECT_OK(dut_.Init());

  EXPECT_OK(dut_.ProbeSdio());

  EXPECT_EQ(sdmmc_.signal_voltage(), SDMMC_VOLTAGE_V180);
  EXPECT_EQ(sdmmc_.bus_width(), SDMMC_BUS_WIDTH_FOUR);
  EXPECT_EQ(sdmmc_.bus_freq(), 50'000'000);
  EXPECT_EQ(sdmmc_.timing(), SDMMC_TIMING_HS);
}

TEST_F(SdioControllerDeviceTest, ProbeSetVoltage) {
  sdmmc_.set_command_callback(
      SDIO_SEND_OP_COND, [](sdmmc_req_t* req) -> void { req->response[0] = OpCondFunctions(5); });

  EXPECT_OK(dut_.ProbeSdio());
  // Card does not report 1.8V support so we don't request a change from the host.
  EXPECT_EQ(sdmmc_.signal_voltage(), SDMMC_VOLTAGE_MAX);

  sdmmc_.set_command_callback(SDIO_SEND_OP_COND, [](sdmmc_req_t* req) -> void {
    req->response[0] = OpCondFunctions(5) | SDIO_SEND_OP_COND_RESP_S18A;
  });

  EXPECT_OK(dut_.ProbeSdio());
  EXPECT_EQ(sdmmc_.signal_voltage(), SDMMC_VOLTAGE_V180);
}

TEST_F(SdioControllerDeviceTest, IoAbortSetsAbortFlag) {
  sdmmc_.set_command_callback(
      SDIO_SEND_OP_COND, [](sdmmc_req_t* req) -> void { req->response[0] = OpCondFunctions(5); });

  EXPECT_OK(dut_.ProbeSdio());

  sdmmc_.set_command_callback(SDIO_IO_RW_DIRECT, [](sdmmc_req_t* req) -> void {
    EXPECT_EQ(req->cmd_idx, SDIO_IO_RW_DIRECT);
    EXPECT_FALSE(req->cmd_flags & SDMMC_CMD_TYPE_ABORT);
    EXPECT_EQ(req->arg, 0xb024'68ab);
  });
  EXPECT_OK(dut_.SdioDoRwByte(true, 3, 0x1234, 0xab, nullptr));

  sdmmc_.set_command_callback(SDIO_IO_RW_DIRECT, [](sdmmc_req_t* req) -> void {
    EXPECT_EQ(req->cmd_idx, SDIO_IO_RW_DIRECT);
    EXPECT_TRUE(req->cmd_flags & SDMMC_CMD_TYPE_ABORT);
    EXPECT_EQ(req->arg, 0x8000'0c03);
  });
  EXPECT_OK(dut_.SdioIoAbort(3));
}

TEST_F(SdioControllerDeviceTest, DifferentManufacturerProductIds) {
  fbl::AutoCall stop_thread([&]() { dut_.StopSdioIrqThread(); });

  sdmmc_.set_command_callback(
      SDIO_SEND_OP_COND, [](sdmmc_req_t* req) -> void { req->response[0] = OpCondFunctions(4); });

  EXPECT_OK(dut_.Init());

  // Function 0-4 CIS pointers.
  sdmmc_.Write(0x0000'0009, std::vector<uint8_t>{0xf5, 0x61, 0x01}, 0);
  sdmmc_.Write(0x0000'0109, std::vector<uint8_t>{0xa0, 0x56, 0x00}, 0);
  sdmmc_.Write(0x0000'0209, std::vector<uint8_t>{0xe9, 0xc3, 0x00}, 0);
  sdmmc_.Write(0x0000'0309, std::vector<uint8_t>{0xb7, 0x6e, 0x01}, 0);
  sdmmc_.Write(0x0000'0409, std::vector<uint8_t>{0x86, 0xb7, 0x00}, 0);

  sdmmc_.Write(0x0001'61f5,
               std::vector<uint8_t>{
                   0x20,        // Manufacturer ID tuple.
                   0x04,        // Manufacturer ID tuple size.
                   0xef, 0xbe,  // Manufacturer code.
                   0xfe, 0xca,  // Manufacturer information (part number/revision).
                   0xff,        // End-of-chain tuple.
               },
               0);

  sdmmc_.Write(0x0000'56a0,
               std::vector<uint8_t>{
                   0x20,
                   0x04,
                   0x7b,
                   0x31,
                   0x8f,
                   0xa8,
                   0xff,
               },
               0);

  sdmmc_.Write(0x0000'c3e9,
               std::vector<uint8_t>{
                   0x20,
                   0x04,
                   0xbd,
                   0x6d,
                   0x0d,
                   0x24,
                   0xff,
               },
               0);

  sdmmc_.Write(0x0001'6eb7,
               std::vector<uint8_t>{
                   0x20,
                   0x04,
                   0xca,
                   0xb8,
                   0x52,
                   0x98,
                   0xff,
               },
               0);

  sdmmc_.Write(0x0000'b786,
               std::vector<uint8_t>{
                   0x20,
                   0x04,
                   0xee,
                   0xf5,
                   0xde,
                   0x30,
                   0xff,
               },
               0);

  EXPECT_OK(dut_.ProbeSdio());

  sdio_hw_info_t info = {};
  EXPECT_OK(dut_.SdioGetDevHwInfo(&info));

  EXPECT_EQ(info.dev_hw_info.num_funcs, 5);
  EXPECT_EQ(info.funcs_hw_info[0].manufacturer_id, 0xbeef);
  EXPECT_EQ(info.funcs_hw_info[0].product_id, 0xcafe);

  Bind ddk;
  EXPECT_OK(dut_.AddDevice());

  dut_.DdkAsyncRemove();
  ddk.Ok();

  constexpr zx_device_prop_t kExpectedProps[4][3] = {
      [0] =
          {
              {BIND_SDIO_VID, 0, 0x317b},
              {BIND_SDIO_PID, 0, 0xa88f},
              {BIND_SDIO_FUNCTION, 0, 1},
          },
      [1] =
          {
              {BIND_SDIO_VID, 0, 0x6dbd},
              {BIND_SDIO_PID, 0, 0x240d},
              {BIND_SDIO_FUNCTION, 0, 2},
          },
      [2] =
          {
              {BIND_SDIO_VID, 0, 0xb8ca},
              {BIND_SDIO_PID, 0, 0x9852},
              {BIND_SDIO_FUNCTION, 0, 3},
          },
      [3] =
          {
              {BIND_SDIO_VID, 0, 0xf5ee},
              {BIND_SDIO_PID, 0, 0x30de},
              {BIND_SDIO_FUNCTION, 0, 4},
          },
  };

  EXPECT_EQ(ddk.total_children(), std::size(kExpectedProps));

  for (size_t i = 0; i < std::size(kExpectedProps); i++) {
    fbl::Span child = ddk.GetChildProps(i);
    ASSERT_EQ(child.size(), std::size(kExpectedProps[0]));
    for (size_t j = 0; j < std::size(kExpectedProps[0]); j++) {
      const zx_device_prop_t& prop = child[j];
      EXPECT_EQ(prop.id, kExpectedProps[i][j].id);
      EXPECT_EQ(prop.reserved, kExpectedProps[i][j].reserved);
      EXPECT_EQ(prop.value, kExpectedProps[i][j].value);
    }
  }
}

}  // namespace sdmmc
