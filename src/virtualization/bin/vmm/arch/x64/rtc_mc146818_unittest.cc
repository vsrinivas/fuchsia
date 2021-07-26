// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/arch/x64/rtc_mc146818.h"

#include <optional>

#include <gtest/gtest.h>

namespace {

class TestRtcMc146818 : public RtcMc146818 {
 public:
  time_t now;

 protected:
  virtual std::chrono::time_point<std::chrono::system_clock> Now() const override {
    return std::chrono::system_clock::from_time_t(now);
  }
};

std::optional<uint8_t> ReadRegister(RtcMc146818& dut, RtcMc146818::Register reg) {
  uint8_t value;
  if (dut.ReadRegister(reg, &value) != ZX_OK)
    return std::nullopt;
  return value;
}

TEST(RtcMc146818Test, RegistersOnReset) {
  // 1626889889 == 2021-07-21T17:51:29Z (Wednesday)
  TestRtcMc146818 dut;
  dut.now = 1626889889;

  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kSeconds),      0x29);
  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kSecondsAlarm), 0x00);
  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kMinutes),      0x51);
  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kMinutesAlarm), 0x00);
  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kHours),        0x17);
  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kHoursAlarm),   0x00);
  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kDayOfWeek),    0x04);
  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kDayOfMonth),   0x21);
  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kMonth),        0x07);
  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kYear),         0x21);
  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kCentury),      0x20);

  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kA), 0b00100000); // Tick rate: 1 second per second
  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kB), 0b00000010); // 24 hour clock
  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kC), 0b00000000);
}

TEST(RtcMc146818Test, UnwritableRegisters) {
  RtcMc146818 dut;

  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kA), 0b00100000);
  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kB), 0b00000010);
  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kC), 0b00000000);

  EXPECT_EQ(dut.WriteRegister(RtcMc146818::Register::kA, 0b11011111), ZX_OK);
  EXPECT_EQ(dut.WriteRegister(RtcMc146818::Register::kB, 0b00001101), ZX_OK);
  EXPECT_EQ(dut.WriteRegister(RtcMc146818::Register::kC, 0b11111111), ZX_OK);
  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kA), 0b00100000);
  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kB), 0b00000010);
  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kC), 0b00000000);
}

TEST(RtcMc146818Test, InvalidRegisters) {
  RtcMc146818 dut;
  for (uint16_t r = 0; r <= 255; ++r) {
    if (RtcMc146818::IsValidRegister(r))
      continue;
    auto reg = static_cast<RtcMc146818::Register>(r);
    uint8_t value;
    EXPECT_EQ(dut.ReadRegister(reg, &value), ZX_ERR_NOT_SUPPORTED);
    EXPECT_EQ(dut.WriteRegister(reg, 0), ZX_ERR_NOT_SUPPORTED);
  }
}

TEST(RtcMc146818Test, UpdateTime) {
  TestRtcMc146818 dut;
  dut.now = 1626889889;

  // 808522787 == 1995-08-15T21:39:47Z (Tuesday)
  uint8_t value;
  EXPECT_EQ(dut.ReadRegister(RtcMc146818::Register::kB, &value), ZX_OK);
  EXPECT_EQ(dut.WriteRegister(RtcMc146818::Register::kB, value | (1 << 7)), ZX_OK); // B = B | SET
  EXPECT_EQ(dut.WriteRegister(RtcMc146818::Register::kSeconds,    0x47), ZX_OK);
  EXPECT_EQ(dut.WriteRegister(RtcMc146818::Register::kMinutes,    0x39), ZX_OK);
  EXPECT_EQ(dut.WriteRegister(RtcMc146818::Register::kHours,      0x21), ZX_OK);
  EXPECT_EQ(dut.WriteRegister(RtcMc146818::Register::kDayOfWeek,  0x03), ZX_OK);

  // Time passing during a time change should not cause incorrect results
  ++dut.now;
  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kSeconds),   0x47);
  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kMinutes),   0x39);
  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kHours),     0x21);
  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kDayOfWeek), 0x03);

  EXPECT_EQ(dut.WriteRegister(RtcMc146818::Register::kDayOfMonth, 0x15), ZX_OK);
  EXPECT_EQ(dut.WriteRegister(RtcMc146818::Register::kMonth,      0x08), ZX_OK);
  EXPECT_EQ(dut.WriteRegister(RtcMc146818::Register::kYear,       0x95), ZX_OK);
  EXPECT_EQ(dut.WriteRegister(RtcMc146818::Register::kCentury,    0x19), ZX_OK);
  EXPECT_EQ(dut.WriteRegister(RtcMc146818::Register::kB, value & ~(1 << 7)), ZX_OK); // B = B & ~SET

  dut.now += 20;
  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kSeconds),    0x07);
  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kMinutes),    0x40);
  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kHours),      0x21);
  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kDayOfWeek),  0x03);
  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kDayOfMonth), 0x15);
  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kMonth),      0x08);
  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kYear),       0x95);
  EXPECT_EQ(ReadRegister(dut, RtcMc146818::Register::kCentury),    0x19);
}

}  // namespace
