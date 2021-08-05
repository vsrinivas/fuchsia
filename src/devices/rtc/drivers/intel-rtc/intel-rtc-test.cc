// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/rtc/drivers/intel-rtc/intel-rtc.h"

#include <librtc.h>

#include <ddktl/device.h>
#include <zxtest/zxtest.h>

#include "fuchsia/hardware/rtc/llcpp/fidl.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

class IntelRtcTest;
IntelRtcTest* CUR_TEST = nullptr;

constexpr uint16_t kPortBase = 0x20;

using intel_rtc::Registers;

class IntelRtcTest : public zxtest::Test {
 public:
  void SetUp() override {
    ASSERT_EQ(CUR_TEST, nullptr);
    CUR_TEST = this;

    fake_root_ = MockDevice::FakeRootParent();
    device_ = std::make_unique<intel_rtc::RtcDevice>(fake_root_.get(), zx::resource(), kPortBase);
  }

  void TearDown() override { CUR_TEST = nullptr; }

  // Set the (fake) time.
  // The hour should be either in the range 0-23 if is_24hr is set or else 1-12.
  void SetTime(uint8_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute,
               uint8_t second, bool bcd, bool is_24hr, bool pm = false) {
    registers_[Registers::kRegYear] = bcd ? to_bcd(year - 2000) : year - 2000;
    registers_[Registers::kRegMonth] = bcd ? to_bcd(month) : month;
    registers_[Registers::kRegDayOfMonth] = bcd ? to_bcd(day) : day;
    registers_[Registers::kRegMinutes] = bcd ? to_bcd(minute) : minute;
    registers_[Registers::kRegSeconds] = bcd ? to_bcd(second) : second;

    registers_[Registers::kRegHours] = is_24hr ? 0 : (pm ? intel_rtc::kHourPmBit : 0);
    registers_[Registers::kRegHours] |= bcd ? to_bcd(hour) : hour;

    SetBcdAnd24Hr(bcd, is_24hr);
  }

  void SetBcdAnd24Hr(bool bcd, bool is_24hr) {
    registers_[Registers::kRegB] = bcd ? 0 : intel_rtc::kRegBDataFormatBit;
    registers_[Registers::kRegB] |= is_24hr ? intel_rtc::kRegBHourFormatBit : 0;
  }

  void ExpectTime(rtc::FidlRtc::wire::Time time, bool bcd, bool is_24hr) {
    auto reg_year = registers_[Registers::kRegYear];
    auto reg_month = registers_[Registers::kRegMonth];
    auto reg_day = registers_[Registers::kRegDayOfMonth];
    auto reg_hours = registers_[Registers::kRegHours];
    auto reg_minute = registers_[Registers::kRegMinutes];
    auto reg_seconds = registers_[Registers::kRegSeconds];

    bool pm = false;
    if (is_24hr) {
      ASSERT_EQ(reg_hours & intel_rtc::kHourPmBit, 0);
    } else if (time.hours > 11) {
      ASSERT_EQ(reg_hours & intel_rtc::kHourPmBit, intel_rtc::kHourPmBit);
      pm = true;
      reg_hours &= ~intel_rtc::kHourPmBit;
    }

    if (bcd) {
      reg_year = from_bcd(reg_year);
      reg_month = from_bcd(reg_month);
      reg_day = from_bcd(reg_day);
      reg_hours = from_bcd(reg_hours);
      reg_minute = from_bcd(reg_minute);
      reg_seconds = from_bcd(reg_seconds);
    }

    if (!is_24hr) {
      if (pm) {
        reg_hours += 12;
      }
      // Fix 12PM and 12AM.
      if (reg_hours == 24 || reg_hours == 12) {
        reg_hours -= 12;
      }
    }

    ASSERT_EQ(reg_year + 2000, time.year);
    ASSERT_EQ(reg_month, time.month);
    ASSERT_EQ(reg_day, time.day);
    ASSERT_EQ(reg_hours, time.hours);
    ASSERT_EQ(reg_minute, time.minutes);
    ASSERT_EQ(reg_seconds, time.seconds);
  }

  void Set(uint8_t index, uint8_t val) {
    ASSERT_LT(index, countof(registers_));
    registers_[index] = val;
  }

  uint8_t Get(uint8_t index) {
    ZX_ASSERT(index < countof(registers_));
    if (index == Registers::kRegA && update_in_progress_count_ > 0) {
      update_in_progress_count_--;
      return intel_rtc::kRegAUpdateInProgressBit;
    }
    return registers_[index];
  }

 protected:
  std::shared_ptr<MockDevice> fake_root_;
  std::unique_ptr<intel_rtc::RtcDevice> device_;

  uint8_t registers_[Registers::kRegD + 1] = {0};
  uint8_t update_in_progress_count_ = 0;
};

// Hooks used by driver code.
namespace intel_rtc {

static std::optional<uint8_t> next_reg_index;

void TestOutp(uint16_t port, uint8_t value) {
  ASSERT_NE(CUR_TEST, nullptr);
  ASSERT_GE(port, kPortBase);
  switch (port - kPortBase) {
    case kIndexOffset:
      next_reg_index = value;
      break;
    case kDataOffset:
      ASSERT_TRUE(next_reg_index.has_value());
      CUR_TEST->Set(next_reg_index.value(), value);
      next_reg_index.reset();
      break;
  }
}

uint8_t TestInp(uint16_t port) {
  ZX_ASSERT(CUR_TEST != nullptr);
  ZX_ASSERT(port >= kPortBase);
  ZX_ASSERT(port - kPortBase == kDataOffset);
  ZX_ASSERT(next_reg_index.has_value());
  return CUR_TEST->Get(next_reg_index.value());
}

}  // namespace intel_rtc

TEST_F(IntelRtcTest, TestReadWriteBinary24Hr) {
  SetTime(2021, 8, 5, 0, 10, 32, /*bcd=*/false, /*is_24hr=*/true);

  auto time = device_->ReadTime();
  ASSERT_EQ(time.hours, 0);
  ASSERT_EQ(time.minutes, 10);
  ASSERT_EQ(time.seconds, 32);

  ASSERT_EQ(time.year, 2021);
  ASSERT_EQ(time.month, 8);
  ASSERT_EQ(time.day, 5);

  device_->WriteTime(time);
  ASSERT_NO_FATAL_FAILURES(ExpectTime(time, /*bcd=*/false, /*is_24hr=*/true));
}

TEST_F(IntelRtcTest, TestReadWriteBcd24Hr) {
  SetTime(2021, 8, 5, 0, 10, 32, /*bcd=*/true, /*is_24hr=*/true);

  auto time = device_->ReadTime();
  ASSERT_EQ(time.hours, 0);
  ASSERT_EQ(time.minutes, 10);
  ASSERT_EQ(time.seconds, 32);

  ASSERT_EQ(time.year, 2021);
  ASSERT_EQ(time.month, 8);
  ASSERT_EQ(time.day, 5);

  device_->WriteTime(time);
  ASSERT_NO_FATAL_FAILURES(ExpectTime(time, /*bcd=*/true, /*is_24hr=*/true));
}

TEST_F(IntelRtcTest, TestReadWriteBinary12Hr) {
  SetTime(2021, 8, 5, 12, 10, 32, /*bcd=*/false, /*is_24hr=*/false, /*pm=*/true);

  auto time = device_->ReadTime();
  ASSERT_EQ(time.hours, 12);
  ASSERT_EQ(time.minutes, 10);
  ASSERT_EQ(time.seconds, 32);

  ASSERT_EQ(time.year, 2021);
  ASSERT_EQ(time.month, 8);
  ASSERT_EQ(time.day, 5);
  device_->WriteTime(time);
  ASSERT_NO_FATAL_FAILURES(ExpectTime(time, /*bcd=*/false, /*is_24hr=*/false));
}

TEST_F(IntelRtcTest, TestReadWriteBcd12Hr) {
  SetTime(2021, 8, 5, 12, 10, 32, /*bcd=*/true, /*is_24hr=*/false, /*pm=*/true);

  auto time = device_->ReadTime();
  ASSERT_EQ(time.hours, 12);
  ASSERT_EQ(time.minutes, 10);
  ASSERT_EQ(time.seconds, 32);

  ASSERT_EQ(time.year, 2021);
  ASSERT_EQ(time.month, 8);
  ASSERT_EQ(time.day, 5);
  device_->WriteTime(time);
  ASSERT_NO_FATAL_FAILURES(ExpectTime(time, /*bcd=*/true, /*is_24hr=*/false));
}

TEST_F(IntelRtcTest, TestReadWrite12HrMidnight) {
  SetTime(2021, 8, 5, 12, 10, 32, /*bcd=*/false, /*is_24hr=*/false, /*pm=*/false);

  auto time = device_->ReadTime();
  ASSERT_EQ(time.hours, 0);
  ASSERT_EQ(time.minutes, 10);
  ASSERT_EQ(time.seconds, 32);

  ASSERT_EQ(time.year, 2021);
  ASSERT_EQ(time.month, 8);
  ASSERT_EQ(time.day, 5);
  device_->WriteTime(time);
  ASSERT_NO_FATAL_FAILURES(ExpectTime(time, /*bcd=*/false, /*is_24hr=*/false));
}

TEST_F(IntelRtcTest, TestReadWaitsForUpdate) {
  SetTime(2021, 8, 5, 12, 10, 32, /*bcd=*/false, /*is_24hr=*/false, /*pm=*/false);
  update_in_progress_count_ = 3;

  device_->ReadTime();
  ASSERT_EQ(update_in_progress_count_, 0);
}
