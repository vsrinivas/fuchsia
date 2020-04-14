// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vs680-power.h"

#include <lib/fake-i2c/fake-i2c.h>

#include <soc/vs680/vs680-power.h>
#include <zxtest/zxtest.h>

namespace power {

class FakePmic : public fake_i2c::FakeI2c {
 public:
  FakePmic() {}
  ~FakePmic() {}

  uint8_t Read(uint8_t address) const { return registers_[address]; }
  void Write(uint8_t value, uint8_t address) { registers_[address] = value; }

 protected:
  zx_status_t Transact(const uint8_t* write_buffer, size_t write_buffer_size, uint8_t* read_buffer,
                       size_t* read_buffer_size) override {
    // Valid transactions are write address, write data or write address, read data. Valid addresses
    // are 0x00 and 0x01.
    if (write_buffer_size < 1 || write_buffer_size > 2 || write_buffer[0] > 1) {
      return ZX_ERR_IO;
    }

    const uint8_t address = write_buffer[0];
    if (write_buffer_size == 1) {
      *read_buffer_size = 1;
      read_buffer[0] = registers_[address];
    } else {
      *read_buffer_size = 0;
      registers_[address] = write_buffer[1];
    }

    return ZX_OK;
  }

 private:
  uint8_t registers_[2] = {};
};

TEST(Vs680PowerTest, RequestVoltage) {
  FakePmic fake_pmic;
  Vs680Power dut(nullptr, ddk::I2cProtocolClient(fake_pmic.GetProto()));

  uint32_t actual_voltage;

  fake_pmic.Write(0b1000'0000, 0);
  fake_pmic.Write(0b1010'1010, 1);
  EXPECT_OK(dut.PowerImplRequestVoltage(vs680::kPowerDomainVCpu, 1'000'000, &actual_voltage));
  EXPECT_EQ(actual_voltage, 1'000'000);
  EXPECT_EQ(fake_pmic.Read(0), 0b0010'1000);
  EXPECT_EQ(fake_pmic.Read(1), 0b1110'1010);

  fake_pmic.Write(0b0001'0101, 1);
  EXPECT_OK(dut.PowerImplRequestVoltage(vs680::kPowerDomainVCpu, 1'870'000, &actual_voltage));
  EXPECT_EQ(actual_voltage, 1'870'000);
  EXPECT_EQ(fake_pmic.Read(0), 0b0111'1111);
  EXPECT_EQ(fake_pmic.Read(1), 0b0101'0101);

  fake_pmic.Write(0b0000'0000, 1);
  EXPECT_OK(dut.PowerImplRequestVoltage(vs680::kPowerDomainVCpu, 600'000, &actual_voltage));
  EXPECT_EQ(actual_voltage, 600'000);
  EXPECT_EQ(fake_pmic.Read(0), 0b0000'0000);
  EXPECT_EQ(fake_pmic.Read(1), 0b0100'0000);
}

TEST(Vs680PowerTest, RequestVoltageGoBitCleared) {
  FakePmic fake_pmic;
  Vs680Power dut(nullptr, ddk::I2cProtocolClient(fake_pmic.GetProto()));

  uint32_t actual_voltage;

  fake_pmic.Write(0b1001'1110, 0);
  fake_pmic.Write(0b0100'0000, 1);
  EXPECT_OK(dut.PowerImplRequestVoltage(vs680::kPowerDomainVCpu, 950'000, &actual_voltage));
  EXPECT_EQ(actual_voltage, 950'000);
  EXPECT_EQ(fake_pmic.Read(0), 0b0010'0011);
  EXPECT_EQ(fake_pmic.Read(1), 0b0000'0000);

  fake_pmic.Write(0b0100'0000, 1);
  EXPECT_OK(dut.PowerImplRequestVoltage(vs680::kPowerDomainVCpu, 910'000, &actual_voltage));
  EXPECT_EQ(actual_voltage, 910'000);
  EXPECT_EQ(fake_pmic.Read(0), 0b0001'1111);
  EXPECT_EQ(fake_pmic.Read(1), 0b0000'0000);

  fake_pmic.Write(0b0100'0000, 1);
  EXPECT_OK(dut.PowerImplRequestVoltage(vs680::kPowerDomainVCpu, 930'000, &actual_voltage));
  EXPECT_EQ(actual_voltage, 930'000);
  EXPECT_EQ(fake_pmic.Read(0), 0b0010'0001);
  EXPECT_EQ(fake_pmic.Read(1), 0b0000'0000);

  fake_pmic.Write(0b0100'0000, 1);
  EXPECT_OK(dut.PowerImplRequestVoltage(vs680::kPowerDomainVCpu, 920'000, &actual_voltage));
  EXPECT_EQ(actual_voltage, 920'000);
  EXPECT_EQ(fake_pmic.Read(0), 0b0010'0000);
  EXPECT_EQ(fake_pmic.Read(1), 0b0000'0000);
}

TEST(Vs680PowerTest, RequestInvalidVoltage) {
  FakePmic fake_pmic;
  Vs680Power dut(nullptr, ddk::I2cProtocolClient(fake_pmic.GetProto()));

  uint32_t actual_voltage;
  EXPECT_NOT_OK(dut.PowerImplRequestVoltage(vs680::kPowerDomainVCpu, 951'000, &actual_voltage));
}

TEST(Vs680PowerTest, GetCurrentVoltage) {
  FakePmic fake_pmic;
  Vs680Power dut(nullptr, ddk::I2cProtocolClient(fake_pmic.GetProto()));

  uint32_t voltage;

  fake_pmic.Write(0b1101'1111, 0);
  EXPECT_OK(dut.PowerImplGetCurrentVoltage(vs680::kPowerDomainVCpu, &voltage));
  EXPECT_EQ(voltage, 800'000);

  fake_pmic.Write(0b0101'1111, 0);
  EXPECT_OK(dut.PowerImplGetCurrentVoltage(vs680::kPowerDomainVCpu, &voltage));
  EXPECT_EQ(voltage, 1'550'000);

  fake_pmic.Write(0b0011'0001, 0);
  EXPECT_OK(dut.PowerImplGetCurrentVoltage(vs680::kPowerDomainVCpu, &voltage));
  EXPECT_EQ(voltage, 1'090'000);

  fake_pmic.Write(0b0000'0000, 0);
  EXPECT_OK(dut.PowerImplGetCurrentVoltage(vs680::kPowerDomainVCpu, &voltage));
  EXPECT_EQ(voltage, 600'000);

  fake_pmic.Write(0b0111'1111, 0);
  EXPECT_OK(dut.PowerImplGetCurrentVoltage(vs680::kPowerDomainVCpu, &voltage));
  EXPECT_EQ(voltage, 1'870'000);

  fake_pmic.Write(0b1111'1111, 0);
  EXPECT_OK(dut.PowerImplGetCurrentVoltage(vs680::kPowerDomainVCpu, &voltage));
  EXPECT_EQ(voltage, 800'000);
}

}  // namespace power
