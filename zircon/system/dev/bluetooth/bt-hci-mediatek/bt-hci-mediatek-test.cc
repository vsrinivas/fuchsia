// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bt-hci-mediatek.h"

#include <lib/mock-function/mock-function.h>
#include <lib/mock-sdio/mock-sdio.h>
#include <zxtest/zxtest.h>

namespace bluetooth {

class BtHciMediatekTest : public BtHciMediatek {
 public:
  struct CardTxnResult {
    zx_status_t status;
    uint32_t data;
  };

  struct CardBlockTxnResult {
    zx_status_t status;
    const uint8_t* data;
    size_t size;
  };

  BtHciMediatekTest(const ddk::SdioProtocolClient& sdio, size_t fw_part_max_size = 0)
      : BtHciMediatek(nullptr, sdio, zx::port(), fw_part_max_size) {}

  mock_function::MockFunction<CardTxnResult, uint32_t>& mock_CardRead32() { return mock_read32_; }
  mock_function::MockFunction<zx_status_t, uint32_t, uint32_t>& mock_CardWrite32() {
    return mock_write32_;
  }
  mock_function::MockFunction<CardTxnResult>& mock_CardRecvPacket() { return mock_recv_packet_; }
  mock_function::MockFunction<zx_status_t>& mock_CardReset() { return mock_reset_; }
  mock_function::MockFunction<CardTxnResult>& mock_CardGetHwVersion() {
    return mock_get_hw_version_;
  }
  mock_function::MockFunction<CardBlockTxnResult, FirmwarePartMode>& mock_CardSendFirmwarePart() {
    return mock_send_firmware_part_;
  }

  void VerifyAll() {
    mock_read32_.VerifyAndClear();
    mock_write32_.VerifyAndClear();
    mock_recv_packet_.VerifyAndClear();
    mock_reset_.VerifyAndClear();
    mock_get_hw_version_.VerifyAndClear();
    mock_send_firmware_part_.VerifyAndClear();
  }

  zx_status_t CardRecvPacket(uint32_t* size) override {
    if (mock_recv_packet_.HasExpectations()) {
      CardTxnResult result = mock_recv_packet_.Call();
      *size = result.data;
      return result.status;
    } else {
      return BtHciMediatek::CardRecvPacket(size);
    }
  }

  zx_status_t CardSendFirmwarePart(zx_handle_t vmo, uint8_t* buffer, const uint8_t* fw_data,
                                   size_t size, FirmwarePartMode mode) override {
    if (mock_send_firmware_part_.HasExpectations()) {
      CardBlockTxnResult result = mock_send_firmware_part_.Call(mode);
      CardSendFirmwarePartHelper(result, fw_data, size);
      return result.status;
    } else {
      return BtHciMediatek::CardSendFirmwarePart(vmo, buffer, fw_data, size, mode);
    }
  }

 private:
  zx_status_t CardRead32(uint32_t address, uint32_t* value) override {
    CardTxnResult result = mock_read32_.Call(address);
    *value = result.data;
    return result.status;
  }

  zx_status_t CardWrite32(uint32_t address, uint32_t value) override {
    return mock_write32_.Call(address, value);
  }

  zx_status_t CardReset() override { return mock_reset_.Call(); }

  zx_status_t CardGetHwVersion(uint32_t* version) override {
    CardTxnResult result = mock_get_hw_version_.Call();
    *version = result.data;
    return result.status;
  }

  void CardSendFirmwarePartHelper(const CardBlockTxnResult& result, const uint8_t* fw_data,
                                  size_t size) {
    ASSERT_EQ(result.size, size);
    EXPECT_BYTES_EQ(result.data, fw_data, result.size);
  }

  mock_function::MockFunction<CardTxnResult, uint32_t> mock_read32_;
  mock_function::MockFunction<zx_status_t, uint32_t, uint32_t> mock_write32_;
  mock_function::MockFunction<CardTxnResult> mock_recv_packet_;
  mock_function::MockFunction<zx_status_t> mock_reset_;
  mock_function::MockFunction<CardTxnResult> mock_get_hw_version_;
  mock_function::MockFunction<CardBlockTxnResult, FirmwarePartMode> mock_send_firmware_part_;
};

TEST(BtHciMediatekTest, TestCardRecvPacket) {
  mock_sdio::MockSdio sdio;
  BtHciMediatekTest test(ddk::SdioProtocolClient(sdio.GetProto()));

  test.mock_CardRead32().ExpectCall({ZX_OK, 0xabcd1236}, 0x10);
  test.mock_CardWrite32().ExpectCall(ZX_OK, 0x10, 0x00001232);

  uint32_t size = 0;
  EXPECT_OK(test.CardRecvPacket(&size));
  EXPECT_EQ(0xabcd, size);

  test.VerifyAll();

  // Test a timeout condition.
  test.mock_CardRead32()
      .ExpectCall({ZX_OK, 0xabcd0000}, 0x10)
      .ExpectCall({ZX_OK, 0x00000000}, 0x10)
      .ExpectCall({ZX_OK, 0x00000000}, 0x10)
      .ExpectCall({ZX_OK, 0x00000002}, 0x10)
      .ExpectCall({ZX_OK, 0x12340000}, 0x10)
      .ExpectCall({ZX_OK, 0x00000002}, 0x10);
  test.mock_CardWrite32().ExpectNoCall();

  EXPECT_EQ(ZX_ERR_TIMED_OUT, test.CardRecvPacket(&size));

  test.VerifyAll();
}

TEST(BtHciMediatekTest, TestCardSendVendorPacketCmd) {
  mock_sdio::MockSdio sdio;
  BtHciMediatekTest test(ddk::SdioProtocolClient(sdio.GetProto()));

  fbl::Vector<uint8_t> write_data = {0x10, 0x00, 0x00, 0x00, 0x01, 0xcd, 0xfc, 0x08,
                                     0xe5, 0xd5, 0x4d, 0x4d, 0x4d, 0xad, 0x47, 0xd0};

  fbl::Vector<uint8_t> read_data = {0x14, 0x00, 0x00, 0x00, 0x32, 0xbf, 0x9c, 0x81, 0x70, 0xdd,
                                    0x9a, 0x68, 0xf0, 0x36, 0x06, 0x49, 0x7c, 0x7b, 0x83, 0x21};
  uint8_t expected_read_packet[16];
  memcpy(expected_read_packet, read_data.data() + 4, read_data.size() - 4);

  uint8_t packet[32];
  size_t size = write_data.size() - 8;
  memcpy(packet, write_data.data() + 8, size);

  sdio.ExpectFifoWrite(0x18, std::move(write_data), false);
  test.mock_CardRecvPacket().ExpectCall({ZX_OK, static_cast<uint32_t>(read_data.size())});
  sdio.ExpectFifoRead(0x1c, std::move(read_data), true);

  EXPECT_OK(test.CardSendVendorPacket(1, 0xcd, packet, &size, sizeof(packet)));
  ASSERT_EQ(sizeof(expected_read_packet), size);
  EXPECT_BYTES_EQ(expected_read_packet, packet, sizeof(expected_read_packet));

  test.VerifyAll();
  sdio.VerifyAndClear();
}

TEST(BtHciMediatekTest, TestCardSendVendorPacketAcl) {
  mock_sdio::MockSdio sdio;
  BtHciMediatekTest test(ddk::SdioProtocolClient(sdio.GetProto()));

  fbl::Vector<uint8_t> write_data = {0x19, 0x00, 0x00, 0x00, 0x02, 0xab, 0xfc, 0x10, 0x00,
                                     0x87, 0xae, 0x01, 0x46, 0x3c, 0xd9, 0xd7, 0x68, 0x8b,
                                     0x07, 0x9b, 0xc8, 0xb1, 0xf3, 0x99, 0x13};

  fbl::Vector<uint8_t> read_data = {0x08, 0x00, 0x00, 0x00, 0xc6, 0x7b, 0x1a, 0x84};
  uint8_t expected_read_packet[4];
  memcpy(expected_read_packet, read_data.data() + 4, read_data.size() - 4);

  uint8_t packet[32];
  size_t size = write_data.size() - 9;
  memcpy(packet, write_data.data() + 9, size);

  sdio.ExpectFifoWrite(0x18, std::move(write_data), false);
  test.mock_CardRecvPacket().ExpectCall({ZX_OK, static_cast<uint32_t>(read_data.size())});
  sdio.ExpectFifoRead(0x1c, std::move(read_data), true);

  EXPECT_OK(test.CardSendVendorPacket(2, 0xab, packet, &size, sizeof(packet)));
  ASSERT_EQ(sizeof(expected_read_packet), size);
  EXPECT_BYTES_EQ(expected_read_packet, packet, sizeof(expected_read_packet));

  test.VerifyAll();
  sdio.VerifyAndClear();
}

TEST(BtHciMediatekTest, TestCardSendVendorPacketEdgeCases) {
  mock_sdio::MockSdio sdio;
  BtHciMediatekTest test(ddk::SdioProtocolClient(sdio.GetProto()));

  fbl::Vector<uint8_t> write_data = {0x0c, 0x00, 0x00, 0x00, 0x01, 0xef,
                                     0xfc, 0x04, 0x02, 0xe6, 0x55, 0xa6};

  fbl::Vector<uint8_t> read_data = {0x04, 0x00, 0x00, 0x00};

  uint8_t packet[12];
  size_t size = write_data.size() - 8;
  memcpy(packet, write_data.data() + 8, size);

  sdio.ExpectFifoWrite(0x18, std::move(write_data), false);
  test.mock_CardRecvPacket().ExpectCall({ZX_OK, static_cast<uint32_t>(read_data.size())});
  sdio.ExpectFifoRead(0x1c, std::move(read_data), true);

  EXPECT_OK(test.CardSendVendorPacket(1, 0xef, packet, &size, sizeof(packet)));
  ASSERT_EQ(0, size);

  test.VerifyAll();
  sdio.VerifyAndClear();
}

TEST(BtHciMediatekTest, TestCardSendVendorPacketFail) {
  mock_sdio::MockSdio sdio;
  BtHciMediatekTest test(ddk::SdioProtocolClient(sdio.GetProto()));

  // Received packet is shorter than the SDIO header.
  fbl::Vector<uint8_t> write_data = {0x0c, 0x00, 0x00, 0x00, 0x01, 0x00,
                                     0xfc, 0x04, 0x31, 0x7b, 0xc0, 0x78};

  uint8_t packet[12];
  size_t size = write_data.size() - 8;
  memcpy(packet, write_data.data() + 8, size);

  sdio.ExpectFifoWrite(0x18, std::move(write_data), false);
  test.mock_CardRecvPacket().ExpectCall({ZX_OK, 3});

  EXPECT_NE(ZX_OK, test.CardSendVendorPacket(1, 0x00, packet, &size, sizeof(packet)));

  test.VerifyAll();
  sdio.VerifyAndClear();

  // Received packet does not fit in the provided buffer.
  write_data = {0x0c, 0x00, 0x00, 0x00, 0x01, 0x00, 0xfc, 0x04, 0x25, 0xce, 0xd4, 0xf2};

  size = write_data.size() - 8;
  memcpy(packet, write_data.data() + 8, size);

  sdio.ExpectFifoWrite(0x18, std::move(write_data), false);
  test.mock_CardRecvPacket().ExpectCall({ZX_OK, sizeof(packet) + 5});

  EXPECT_NE(ZX_OK, test.CardSendVendorPacket(1, 0x00, packet, &size, sizeof(packet)));

  test.VerifyAll();
  sdio.VerifyAndClear();

  // Received packet size field doesn't match size field from CHISR register.
  write_data = {0x0c, 0x00, 0x00, 0x00, 0x01, 0x00, 0xfc, 0x04, 0xcd, 0x9b, 0x46, 0xfc};

  fbl::Vector<uint8_t> read_data = {0x06, 0x00, 0x00, 0x00, 0xa3, 0x2f, 0x2c, 0xf0};

  size = write_data.size() - 8;
  memcpy(packet, write_data.data() + 8, size);

  sdio.ExpectFifoWrite(0x18, std::move(write_data), false);
  test.mock_CardRecvPacket().ExpectCall({ZX_OK, static_cast<uint32_t>(read_data.size())});
  sdio.ExpectFifoRead(0x1c, std::move(read_data), true);

  EXPECT_NE(ZX_OK, test.CardSendVendorPacket(1, 0x00, packet, &size, sizeof(packet)));

  test.VerifyAll();
  sdio.VerifyAndClear();
}

TEST(BtHciMediatekTest, TestCardSendFirmwarePart) {
  constexpr uint8_t kExpectedResponse[] = {0x0c, 0x00, 0x00, 0x00, 0x04, 0xe4,
                                           0x05, 0x02, 0x01, 0x01, 0x00, 0x00};
  mock_sdio::MockSdio sdio;
  BtHciMediatekTest test(ddk::SdioProtocolClient(sdio.GetProto()));

  fbl::Vector<uint8_t> write_data = {0x2e, 0x00, 0x00, 0x00,        // SDIO header
                                     0x02, 0x6f, 0xfc, 0x25, 0x00,  // Vendor header
                                     0x01, 0x01, 0x21, 0x00, 0x01,  // STP header
                                     0xc9, 0xad, 0x54, 0x57, 0x36, 0xe3, 0x02, 0x9b,  // Payload
                                     0x71, 0x93, 0x0e, 0x64, 0xfb, 0xe4, 0x5c, 0x94,
                                     0xd5, 0xae, 0x0b, 0x37, 0x45, 0xbf, 0x30, 0x54,
                                     0x52, 0x29, 0x2f, 0x27, 0xa4, 0x74, 0x4f, 0xd9};

  zx::vmo vmo;
  fzl::VmoMapper mapper;
  ASSERT_OK(
      mapper.CreateAndMap(write_data.size(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo));
  uint8_t* buffer = reinterpret_cast<uint8_t*>(mapper.start());

  uint8_t fw_data[32];
  size_t fw_data_size = write_data.size() - 14;
  memcpy(fw_data, write_data.data() + 14, fw_data_size);

  fbl::Vector<uint8_t> read_data;
  for (size_t i = 0; i < sizeof(kExpectedResponse); i++) {
    read_data.push_back(kExpectedResponse[i]);
  }

  sdio.ExpectFifoWrite(0x18, std::move(write_data), false);
  test.mock_CardRecvPacket().ExpectCall({ZX_OK, static_cast<uint32_t>(read_data.size())});
  sdio.ExpectFifoRead(0x1c, std::move(read_data), true);

  EXPECT_OK(test.CardSendFirmwarePart(vmo.get(), buffer, fw_data, fw_data_size,
                                      BtHciMediatek::kFirmwarePartFirst));

  test.VerifyAll();
  sdio.VerifyAndClear();

  write_data = {0x2e, 0x00, 0x00, 0x00, 0x02, 0x6f, 0xfc, 0x25, 0x00, 0x01, 0x01, 0x21,
                0x00, 0x02, 0xe9, 0x39, 0x18, 0x18, 0x6a, 0x3b, 0xd7, 0x1a, 0x94, 0xca,
                0xd2, 0x93, 0xdc, 0x34, 0xbb, 0x86, 0x4a, 0x7d, 0x48, 0x48, 0x6b, 0xfd,
                0xc0, 0xa6, 0xb6, 0x01, 0xe1, 0xec, 0x3f, 0xac, 0x57, 0x2f};

  fw_data_size = write_data.size() - 14;
  memcpy(fw_data, write_data.data() + 14, fw_data_size);

  for (size_t i = 0; i < sizeof(kExpectedResponse); i++) {
    read_data.push_back(kExpectedResponse[i]);
  }

  sdio.ExpectFifoWrite(0x18, std::move(write_data), false);
  test.mock_CardRecvPacket().ExpectCall({ZX_OK, static_cast<uint32_t>(read_data.size())});
  sdio.ExpectFifoRead(0x1c, std::move(read_data), true);

  EXPECT_OK(test.CardSendFirmwarePart(vmo.get(), buffer, fw_data, fw_data_size,
                                      BtHciMediatek::kFirmwarePartContinue));

  test.VerifyAll();
  sdio.VerifyAndClear();

  write_data = {0x16, 0x00, 0x00, 0x00, 0x02, 0x6f, 0xfc, 0x0d, 0x00, 0x01, 0x01,
                0x09, 0x00, 0x03, 0x90, 0x8a, 0x05, 0xd6, 0x68, 0x5c, 0x39, 0x81};

  fw_data_size = write_data.size() - 14;
  memcpy(fw_data, write_data.data() + 14, fw_data_size);

  for (size_t i = 0; i < sizeof(kExpectedResponse); i++) {
    read_data.push_back(kExpectedResponse[i]);
  }

  sdio.ExpectFifoWrite(0x18, std::move(write_data), false);
  test.mock_CardRecvPacket().ExpectCall({ZX_OK, static_cast<uint32_t>(read_data.size())});
  sdio.ExpectFifoRead(0x1c, std::move(read_data), true);

  EXPECT_OK(test.CardSendFirmwarePart(vmo.get(), buffer, fw_data, fw_data_size,
                                      BtHciMediatek::kFirmwarePartLast));

  test.VerifyAll();
  sdio.VerifyAndClear();
}

TEST(BtHciMediatekTest, TestCardSendFirmwarePartFail) {
  mock_sdio::MockSdio sdio;
  BtHciMediatekTest test(ddk::SdioProtocolClient(sdio.GetProto()));

  fbl::Vector<uint8_t> write_data = {0x16, 0x00, 0x00, 0x00, 0x02, 0x6f, 0xfc, 0x0d,
                                     0x00, 0x01, 0x01, 0x09, 0x00, 0x01, 0x5e, 0xe0,
                                     0xb5, 0x7e, 0xf8, 0x90, 0x46, 0xa3};

  zx::vmo vmo;
  fzl::VmoMapper mapper;
  ASSERT_OK(
      mapper.CreateAndMap(write_data.size(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo));
  uint8_t* buffer = reinterpret_cast<uint8_t*>(mapper.start());

  uint8_t fw_data[8];
  size_t fw_data_size = write_data.size() - 14;
  memcpy(fw_data, write_data.data() + 14, fw_data_size);

  sdio.ExpectFifoWrite(0x18, std::move(write_data), false);
  test.mock_CardRecvPacket().ExpectCall({ZX_OK, 13});

  EXPECT_NE(ZX_OK, test.CardSendFirmwarePart(vmo.get(), buffer, fw_data, fw_data_size,
                                             BtHciMediatek::kFirmwarePartFirst));

  test.VerifyAll();
  sdio.VerifyAndClear();

  write_data = {0x16, 0x00, 0x00, 0x00, 0x02, 0x6f, 0xfc, 0x0d, 0x00, 0x01, 0x01,
                0x09, 0x00, 0x03, 0x7c, 0x4b, 0x8b, 0xd2, 0x73, 0x0d, 0x72, 0x8e};

  fw_data_size = write_data.size() - 14;
  memcpy(fw_data, write_data.data() + 14, fw_data_size);

  fbl::Vector<uint8_t> read_data = {0x0c, 0x00, 0x00, 0x00, 0x04, 0xe4,
                                    0x05, 0x02, 0x01, 0x01, 0x00, 0x01};

  sdio.ExpectFifoWrite(0x18, std::move(write_data), false);
  test.mock_CardRecvPacket().ExpectCall({ZX_OK, static_cast<uint32_t>(read_data.size())});
  sdio.ExpectFifoRead(0x1c, std::move(read_data), true);

  EXPECT_NE(ZX_OK, test.CardSendFirmwarePart(vmo.get(), buffer, fw_data, fw_data_size,
                                             BtHciMediatek::kFirmwarePartLast));

  test.VerifyAll();
  sdio.VerifyAndClear();
}

TEST(BtHciMediatekTest, TestCardDownloadFirmware) {
  mock_sdio::MockSdio sdio;
  BtHciMediatekTest test(ddk::SdioProtocolClient(sdio.GetProto()), 8);

  const uint8_t fw_data[] = {0x7f, 0x26, 0x56, 0xd1, 0x44, 0x2c, 0xff, 0x5d,  // Header
                             0xa5, 0xdf, 0x69, 0xf5, 0x0b, 0x7e, 0xc0, 0x6b, 0x50, 0xda, 0x6e,
                             0x0c, 0x6e, 0xb0, 0x75, 0xc3, 0xd9, 0x91, 0xff, 0x92, 0xf4, 0x06,
                             0x89, 0xf0, 0x13, 0x10, 0x7a, 0xa6, 0x52, 0x40,  // Firmware data
                             0x92, 0xe5, 0x96, 0x52, 0x86, 0x14, 0x90, 0x11, 0xd1, 0x94, 0x3f,
                             0x2f, 0xf9, 0x6d, 0xab, 0x9e, 0xa0, 0x59, 0x0a, 0xfd};

  zx::vmo vmo;
  fzl::VmoMapper mapper;
  ASSERT_OK(
      mapper.CreateAndMap(sizeof(fw_data), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo));
  memcpy(mapper.start(), fw_data, sizeof(fw_data));

  test.mock_CardGetHwVersion().ExpectCall({ZX_OK, 0});
  test.mock_CardSendFirmwarePart().ExpectCall({ZX_OK, fw_data + 30, 8},
                                              BtHciMediatek::kFirmwarePartFirst);
  test.mock_CardSendFirmwarePart().ExpectCall({ZX_OK, fw_data + 38, 8},
                                              BtHciMediatek::kFirmwarePartContinue);
  test.mock_CardSendFirmwarePart().ExpectCall({ZX_OK, fw_data + 46, 8},
                                              BtHciMediatek::kFirmwarePartContinue);
  test.mock_CardSendFirmwarePart().ExpectCall({ZX_OK, fw_data + 54, 4},
                                              BtHciMediatek::kFirmwarePartLast);
  test.mock_CardRead32().ExpectCall({ZX_OK, 0}, 0x0c);
  test.mock_CardWrite32().ExpectCall(ZX_OK, 0x0c, 2);
  test.mock_CardReset().ExpectCall(ZX_OK);

  EXPECT_OK(test.CardDownloadFirmware(vmo, sizeof(fw_data)));

  test.VerifyAll();
  sdio.VerifyAndClear();
}

TEST(BtHciMediatekTest, TestCardDownloadFirmwareEdgeCases) {
  mock_sdio::MockSdio sdio;
  BtHciMediatekTest test(ddk::SdioProtocolClient(sdio.GetProto()), 8);

  const uint8_t fw_data[] = {0xb0, 0x4f, 0xe7, 0x35, 0x71, 0x41, 0xc4, 0xac, 0x72, 0x95, 0x63,
                             0x9d, 0x3c, 0x93, 0xfb, 0x0c, 0xee, 0x84, 0x05, 0xbf, 0x98, 0xe5,
                             0xde, 0x30, 0xf8, 0xaf, 0xbf, 0x2c, 0xfd, 0x7f, 0xc4, 0x6b, 0x59,
                             0x3e, 0xc2, 0xc2, 0x77, 0xc4, 0x5e, 0xe1, 0x89, 0xe4, 0x93, 0xf8,
                             0x0b, 0x22, 0x4b, 0x20, 0xc2, 0x9b, 0x0f, 0x6f, 0x0c, 0x4e};

  zx::vmo vmo;
  fzl::VmoMapper mapper;
  ASSERT_OK(
      mapper.CreateAndMap(sizeof(fw_data), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo));
  memcpy(mapper.start(), fw_data, sizeof(fw_data) - 16);

  test.mock_CardGetHwVersion().ExpectCall({ZX_OK, 0});
  test.mock_CardSendFirmwarePart().ExpectCall({ZX_OK, fw_data + 30, 8},
                                              BtHciMediatek::kFirmwarePartFirst);
  test.mock_CardRead32().ExpectCall({ZX_OK, 0}, 0x0c);
  test.mock_CardWrite32().ExpectCall(ZX_OK, 0x0c, 2);
  test.mock_CardReset().ExpectCall(ZX_OK);

  EXPECT_OK(test.CardDownloadFirmware(vmo, sizeof(fw_data) - 16));

  test.VerifyAll();
  sdio.VerifyAndClear();

  memcpy(mapper.start(), fw_data, sizeof(fw_data) - 4);

  test.mock_CardGetHwVersion().ExpectCall({ZX_OK, 0});
  test.mock_CardSendFirmwarePart().ExpectCall({ZX_OK, fw_data + 30, 4},
                                              BtHciMediatek::kFirmwarePartFirst);
  test.mock_CardRead32().ExpectCall({ZX_OK, 0}, 0x0c);
  test.mock_CardWrite32().ExpectCall(ZX_OK, 0x0c, 2);
  test.mock_CardReset().ExpectCall(ZX_OK);

  EXPECT_OK(test.CardDownloadFirmware(vmo, sizeof(fw_data) - 20));

  test.VerifyAll();
  sdio.VerifyAndClear();

  memcpy(mapper.start(), fw_data, sizeof(fw_data));

  test.mock_CardGetHwVersion().ExpectCall({ZX_OK, 0});
  test.mock_CardSendFirmwarePart().ExpectCall({ZX_OK, fw_data + 30, 8},
                                              BtHciMediatek::kFirmwarePartFirst);
  test.mock_CardSendFirmwarePart().ExpectCall({ZX_OK, fw_data + 38, 8},
                                              BtHciMediatek::kFirmwarePartContinue);
  test.mock_CardSendFirmwarePart().ExpectCall({ZX_OK, fw_data + 46, 8},
                                              BtHciMediatek::kFirmwarePartLast);
  test.mock_CardRead32().ExpectCall({ZX_OK, 0}, 0x0c);
  test.mock_CardWrite32().ExpectCall(ZX_OK, 0x0c, 2);
  test.mock_CardReset().ExpectCall(ZX_OK);

  EXPECT_OK(test.CardDownloadFirmware(vmo, sizeof(fw_data)));

  test.VerifyAll();
  sdio.VerifyAndClear();

  memcpy(mapper.start(), fw_data, sizeof(fw_data) - 24);

  test.mock_CardGetHwVersion().ExpectCall({ZX_OK, 0});
  test.mock_CardRead32().ExpectCall({ZX_OK, 0}, 0x0c);
  test.mock_CardWrite32().ExpectCall(ZX_OK, 0x0c, 2);
  test.mock_CardReset().ExpectCall(ZX_OK);

  EXPECT_OK(test.CardDownloadFirmware(vmo, sizeof(fw_data) - 24));

  test.VerifyAll();
  sdio.VerifyAndClear();
}

TEST(BtHciMediatekTest, TestCardDownloadFirmwareFail) {
  mock_sdio::MockSdio sdio;
  BtHciMediatekTest test(ddk::SdioProtocolClient(sdio.GetProto()), 8);

  zx::vmo vmo;
  fzl::VmoMapper mapper;
  ASSERT_OK(mapper.CreateAndMap(32, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo));

  EXPECT_NE(ZX_OK, test.CardDownloadFirmware(vmo, 29));

  test.VerifyAll();
  sdio.VerifyAndClear();

  test.mock_CardGetHwVersion().ExpectCall({ZX_OK, 0x8a00});

  EXPECT_NE(ZX_OK, test.CardDownloadFirmware(vmo, 32));

  test.VerifyAll();
  sdio.VerifyAndClear();
}

}  // namespace bluetooth
