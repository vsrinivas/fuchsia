// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/mock-i2c/mock-i2c.h>

#include <hwreg/i2c.h>

class DummyI2cRegister : public hwreg::I2cRegisterBase<DummyI2cRegister, uint8_t, sizeof(uint8_t)> {
 public:
  DEF_BIT(7, test_bit);
  DEF_FIELD(3, 0, test_field);
  static auto Get() { return hwreg::I2cRegisterAddr<DummyI2cRegister>(0xAB); }
};

TEST(I2cHwregTest, Read) {
  auto dut = DummyI2cRegister::Get().FromValue(0);

  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWrite({0xAB}).ExpectReadStop({0x8A});
  auto proto = ddk::I2cProtocolClient(mock_i2c.GetProto());

  EXPECT_OK(dut.ReadFrom(proto));
  EXPECT_EQ(dut.test_bit(), 1);
  EXPECT_EQ(dut.test_field(), 0xA);

  mock_i2c.VerifyAndClear();
}

TEST(I2cHwregTest, Write) {
  auto dut = DummyI2cRegister::Get().FromValue(0);
  dut.set_test_bit(1);
  dut.set_test_field(0xA);

  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWriteStop({0xAB, 0x8A});
  auto proto = ddk::I2cProtocolClient(mock_i2c.GetProto());

  EXPECT_OK(dut.WriteTo(proto));

  mock_i2c.VerifyAndClear();
}

class I2cRegister3ByteAddress
    : public hwreg::I2cRegisterBase<I2cRegister3ByteAddress, uint8_t, sizeof(uint8_t) * 3,
                                    hwreg::LittleEndian> {
 public:
  DEF_BIT(7, test_bit);
  DEF_FIELD(3, 0, test_field);
  static auto Get() { return hwreg::I2cRegisterAddr<I2cRegister3ByteAddress>(0xABCDEF); }
};

TEST(I2cHwregTest, I2c3ByteAddressRead) {
  auto dut = I2cRegister3ByteAddress::Get().FromValue(0);

  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWrite({0xEF, 0xCD, 0xAB}).ExpectReadStop({0x8A});
  auto proto = ddk::I2cProtocolClient(mock_i2c.GetProto());

  EXPECT_OK(dut.ReadFrom(proto));
  EXPECT_EQ(dut.test_bit(), 1);
  EXPECT_EQ(dut.test_field(), 0xA);

  mock_i2c.VerifyAndClear();
}

TEST(I2cHwregTest, I2c3BytesAddressWrite) {
  auto dut = I2cRegister3ByteAddress::Get().FromValue(0);
  dut.set_test_bit(1);
  dut.set_test_field(0xA);

  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWriteStop({0xEF, 0xCD, 0xAB, 0x8A});
  auto proto = ddk::I2cProtocolClient(mock_i2c.GetProto());

  EXPECT_OK(dut.WriteTo(proto));

  mock_i2c.VerifyAndClear();
}

class I2cBigEndianRegister : public hwreg::I2cRegisterBase<I2cBigEndianRegister, uint16_t,
                                                           sizeof(uint8_t) * 3, hwreg::BigEndian> {
 public:
  DEF_FIELD(15, 8, msb);
  DEF_FIELD(7, 0, lsb);
  static auto Get() { return hwreg::I2cRegisterAddr<I2cBigEndianRegister>(0xABCDEF); }
};

TEST(I2cHwregTest, BigEndianRead) {
  auto dut = I2cBigEndianRegister::Get().FromValue(0);

  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWrite({0xAB, 0xCD, 0xEF}).ExpectReadStop({0x0A, 0x0B});
  auto proto = ddk::I2cProtocolClient(mock_i2c.GetProto());

  EXPECT_OK(dut.ReadFrom(proto));
  EXPECT_EQ(dut.msb(), 0x0A);
  EXPECT_EQ(dut.lsb(), 0x0B);

  mock_i2c.VerifyAndClear();
}

TEST(I2cHwregTest, BigEndianWrite) {
  auto dut = I2cBigEndianRegister::Get().FromValue(0);
  dut.set_msb(0xA);
  dut.set_lsb(0xB);

  mock_i2c::MockI2c mock_i2c;
  mock_i2c.ExpectWriteStop({0xAB, 0xCD, 0xEF, 0x0A, 0x0B});
  auto proto = ddk::I2cProtocolClient(mock_i2c.GetProto());

  EXPECT_OK(dut.WriteTo(proto));

  mock_i2c.VerifyAndClear();
}
