// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mtk-spi.h"

#include <lib/fake_ddk/fake_ddk.h>

#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <mock-mmio-reg/mock-mmio-reg.h>

namespace {

constexpr size_t kRegSize = 0x00001000 / sizeof(uint32_t);  // in 32 bits chunks.

}  // namespace

namespace spi {

class FakeMtkSpi : public MtkSpi {
 public:
  static std::unique_ptr<FakeMtkSpi> Create(ddk::MmioBuffer mmio) {
    fbl::AllocChecker ac;
    auto device = fbl::make_unique_checked<FakeMtkSpi>(&ac, std::move(mmio));
    if (!ac.check()) {
      zxlogf(ERROR, "%s: device object alloc failed", __func__);
      return nullptr;
    }
    EXPECT_OK(device->Init());

    return device;
  }

  explicit FakeMtkSpi(ddk::MmioBuffer mmio) : MtkSpi(nullptr, std::move(mmio)) {}
};

class MtkSpiTest : public zxtest::Test {
 public:
  void SetUp() override {
    fbl::AllocChecker ac;

    regs_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: regs_ alloc failed", __func__);
      return;
    }
    mock_mmio_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(&ac, regs_.get(),
                                                                       sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: mock_mmio_ alloc failed", __func__);
      return;
    }

    (*mock_mmio_)[6 * 4].ExpectRead(0x00000000).ExpectWrite(0x00000004);  // Reset
    (*mock_mmio_)[6 * 4].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFFFFB);
    (*mock_mmio_)[6 * 4].ExpectRead(0x00000000).ExpectWrite(0x00003000);   // MSB
    (*mock_mmio_)[0 * 4].ExpectRead(0x00000000).ExpectWrite(0x00210021);   // CS
    (*mock_mmio_)[10 * 4].ExpectRead(0x00000000).ExpectWrite(0x00100010);  // SCK
    (*mock_mmio_)[1 * 4].ExpectRead(0x00000000).ExpectWrite(0x00000021);   // Idle
    ddk::MmioBuffer mmio(mock_mmio_->GetMmioBuffer());
    spi_ = FakeMtkSpi::Create(std::move(mmio));
    ASSERT_NOT_NULL(spi_);
  }

  void TearDown() override { mock_mmio_->VerifyAll(); }

 protected:
  std::unique_ptr<FakeMtkSpi> spi_;

  // Mmio Regs and Regions
  fbl::Array<ddk_mock::MockMmioReg> regs_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> mock_mmio_;
};

TEST_F(MtkSpiTest, Exchange1) {
  uint8_t txdata[8];
  memset(txdata, 0x01, sizeof(txdata));
  uint8_t rxdata[8] = {0};
  size_t actual = 0;

  (*mock_mmio_)[6 * 4].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFF3FF);  // DMA
  (*mock_mmio_)[1 * 4].ExpectRead(0x00000000).ExpectWrite(0x00070000);  // Packet
  (*mock_mmio_)[4 * 4].ExpectWrite(0x01010101);                         // TX Data
  (*mock_mmio_)[4 * 4].ExpectWrite(0x01010101);
  (*mock_mmio_)[6 * 4].ExpectRead(0x00000000).ExpectWrite(0x00000001);  // Activate
  (*mock_mmio_)[8 * 4].ExpectRead(0x00000001);                          // Wait
  (*mock_mmio_)[5 * 4].ExpectRead(0x04030201);                          // RX Data
  (*mock_mmio_)[5 * 4].ExpectRead(0x08070605);
  EXPECT_OK(spi_->SpiImplExchange(0, txdata, sizeof(txdata), rxdata, sizeof(rxdata), &actual));
  EXPECT_EQ(actual, 8);
  uint8_t expected[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  EXPECT_BYTES_EQ(rxdata, expected, sizeof(expected));
}

TEST_F(MtkSpiTest, Exchange2) {
  uint8_t txdata[48];
  memset(txdata, 0x01, sizeof(txdata));
  uint8_t rxdata[48] = {0};
  size_t actual = 0;

  (*mock_mmio_)[6 * 4].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFF3FF);  // DMA
  (*mock_mmio_)[1 * 4].ExpectRead(0x00000000).ExpectWrite(0x001F0000);  // Packet
  (*mock_mmio_)[4 * 4].ExpectWrite(0x01010101);                         // TX Data
  (*mock_mmio_)[4 * 4].ExpectWrite(0x01010101);
  (*mock_mmio_)[4 * 4].ExpectWrite(0x01010101);
  (*mock_mmio_)[4 * 4].ExpectWrite(0x01010101);
  (*mock_mmio_)[4 * 4].ExpectWrite(0x01010101);
  (*mock_mmio_)[4 * 4].ExpectWrite(0x01010101);
  (*mock_mmio_)[4 * 4].ExpectWrite(0x01010101);
  (*mock_mmio_)[4 * 4].ExpectWrite(0x01010101);
  (*mock_mmio_)[6 * 4].ExpectRead(0x00000000).ExpectWrite(0x00000001);  // Activate
  (*mock_mmio_)[8 * 4].ExpectRead(0x00000000);                          // Wait Fail
  (*mock_mmio_)[8 * 4].ExpectRead(0x00000001);                          // Wait Success
  (*mock_mmio_)[5 * 4].ExpectRead(0x0A0A0A0A);                          // RX Data
  (*mock_mmio_)[5 * 4].ExpectRead(0x0A0A0A0A);
  (*mock_mmio_)[5 * 4].ExpectRead(0x0A0A0A0A);
  (*mock_mmio_)[5 * 4].ExpectRead(0x0A0A0A0A);
  (*mock_mmio_)[5 * 4].ExpectRead(0x0A0A0A0A);
  (*mock_mmio_)[5 * 4].ExpectRead(0x0A0A0A0A);
  (*mock_mmio_)[5 * 4].ExpectRead(0x0A0A0A0A);
  (*mock_mmio_)[5 * 4].ExpectRead(0x0A0A0A0A);

  (*mock_mmio_)[6 * 4].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFF3FF);  // DMA
  (*mock_mmio_)[1 * 4].ExpectRead(0x00000000).ExpectWrite(0x000F0000);  // Packet
  (*mock_mmio_)[4 * 4].ExpectWrite(0x01010101);                         // TX Data
  (*mock_mmio_)[4 * 4].ExpectWrite(0x01010101);
  (*mock_mmio_)[4 * 4].ExpectWrite(0x01010101);
  (*mock_mmio_)[4 * 4].ExpectWrite(0x01010101);
  (*mock_mmio_)[6 * 4].ExpectRead(0x00000000).ExpectWrite(0x00000001);  // Activate
  (*mock_mmio_)[8 * 4].ExpectRead(0x00000001);                          // Wait Success
  (*mock_mmio_)[5 * 4].ExpectRead(0x0A0A0A0A);                          // RX Data
  (*mock_mmio_)[5 * 4].ExpectRead(0x0A0A0A0A);
  (*mock_mmio_)[5 * 4].ExpectRead(0x0A0A0A0A);
  (*mock_mmio_)[5 * 4].ExpectRead(0x0A0A0A0A);
  EXPECT_OK(spi_->SpiImplExchange(0, txdata, sizeof(txdata), rxdata, sizeof(rxdata), &actual));
  EXPECT_EQ(actual, 48);
  uint8_t expected[48];
  memset(expected, 0x0A, sizeof(expected));
  EXPECT_BYTES_EQ(rxdata, expected, sizeof(expected));
}

TEST_F(MtkSpiTest, Exchange3) {
  uint8_t txdata[7];
  memset(txdata, 0x01, sizeof(txdata));
  uint8_t rxdata[7] = {0};
  size_t actual = 0;

  (*mock_mmio_)[6 * 4].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFFF3FF);  // DMA
  (*mock_mmio_)[1 * 4].ExpectRead(0x00000000).ExpectWrite(0x00060000);  // Packet
  (*mock_mmio_)[4 * 4].ExpectWrite(0x01010101);                         // TX Data
  (*mock_mmio_)[4 * 4].ExpectWrite(0x00010101);
  (*mock_mmio_)[6 * 4].ExpectRead(0x00000000).ExpectWrite(0x00000001);  // Activate
  (*mock_mmio_)[8 * 4].ExpectRead(0x00000001);                          // Wait Success
  (*mock_mmio_)[5 * 4].ExpectRead(0x0A0A0A0A);                          // RX Data
  (*mock_mmio_)[5 * 4].ExpectRead(0x0B0A0A0A);

  EXPECT_OK(spi_->SpiImplExchange(0, txdata, sizeof(txdata), rxdata, sizeof(rxdata), &actual));
  EXPECT_EQ(actual, 7);
  uint8_t expected[7];
  memset(expected, 0x0A, sizeof(expected));
  EXPECT_BYTES_EQ(rxdata, expected, sizeof(expected));
}

}  // namespace spi
