// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/arch/x64/rtc_mc146818.h"

#include <errno.h>
#include <lib/syslog/cpp/macros.h>

#include <ctime>

namespace {
enum RegisterBFlags : uint8_t {
  kRegisterBDaylightSavingsEnable = 1 << 0,   // DSE
  kRegisterB24HourFormat = 1 << 1,            // 24/12
  kRegisterBBinaryMode = 1 << 2,              // DM
  kRegisterBSquareWaveEnable = 1 << 3,        // SQWE
  kRegisterBUpdateInteruptEnable = 1 << 4,    // UIE
  kRegisterBAlarmInteruptEnable = 1 << 5,     // AIE
  kRegisterBPeriodicInteruptEnable = 1 << 6,  // PIE
  kRegisterBStopTicks = 1 << 7,               // SET
};
// Alternate/extra RTC modes are unsupported by this emulated RTC, so we
// make them unwriteable
constexpr uint8_t kRegisterBUnwritableMask = kRegisterBDaylightSavingsEnable |
                                             kRegisterB24HourFormat | kRegisterBBinaryMode |
                                             kRegisterBSquareWaveEnable;

enum RegisterCFlags : uint8_t {
  kRegisterCUpdateFlag = 1 << 4,    // UF
  kRegisterCAlarmFlag = 1 << 5,     // AF
  kRegisterCPeriodicFlag = 1 << 6,  // PF
  kRegisterCIRQFlag = 1 << 7,       // IRQF
};

// Linux expects the RTC to be in BCD mode regardless of the binary mode flag
// on x86, so we have to convert registers back and forth
constexpr uint8_t ToBcd(int binary) {
  return static_cast<uint8_t>(((binary / 10 % 10) << 4) | (binary % 10));
}
constexpr uint8_t FromBcd(uint8_t bcd) { return ((bcd & 0xf0) >> 4) * 10 + (bcd & 0x0f); }
}  // namespace

RtcMc146818::RtcMc146818() {
  registers_[Register::kA] = 0b00100000;  // Tick rate: 1 second per second
  registers_[Register::kB] = kRegisterB24HourFormat;
  registers_[Register::kC] = 0;
  registers_[Register::kSecondsAlarm] = 0;
  registers_[Register::kMinutesAlarm] = 0;
  registers_[Register::kHoursAlarm] = 0;
  UpdateTime();
}

zx_status_t RtcMc146818::ReadRegister(Register reg, uint8_t* value) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (registers_.count(reg) == 0) {
    FX_LOGS(ERROR) << "Read from unsupported RTC register (0x" << std::hex
                   << static_cast<uint32_t>(reg) << ")";
    return ZX_ERR_NOT_SUPPORTED;
  }

  UpdateTime();
  *value = registers_[reg];
  if (reg == Register::kC) {
    // Register C is cleared on read
    registers_[Register::kC] = 0;
  }
  return ZX_OK;
}

zx_status_t RtcMc146818::WriteRegister(Register reg, uint8_t value) {
  std::lock_guard<std::mutex> lock(mutex_);
  UpdateTime();
  switch (reg) {
    case Register::kSeconds:
    case Register::kMinutes:
    case Register::kHours:
    case Register::kDayOfWeek:
    case Register::kDayOfMonth:
    case Register::kMonth:
    case Register::kYear:
    case Register::kCentury:
      registers_[reg] = value;
      offset_ = GetOffset();
      break;

    case Register::kSecondsAlarm:
    case Register::kMinutesAlarm:
    case Register::kHoursAlarm:
      // TODO: Implement alarms
      return ZX_ERR_NOT_SUPPORTED;

    case Register::kA:
      // Changing the RTC speed is unsupported
      FX_LOGS(DEBUG) << "Ignoring write to adjust RTC speed (0x" << std::hex
                     << static_cast<uint32_t>(value) << ")";
      break;

    case Register::kB:
      if (value & (kRegisterBUpdateInteruptEnable | kRegisterBAlarmInteruptEnable |
                   kRegisterBPeriodicInteruptEnable)) {
        // Update, alarm, and periodic interrupts are unimplemented
        // TODO: Implement alarms
        return ZX_ERR_NOT_SUPPORTED;
      }
      registers_[Register::kB] = (value & ~kRegisterBUnwritableMask) |
                                 (registers_[Register::kB] & kRegisterBUnwritableMask);
      if (registers_[Register::kB] != value) {
        FX_LOGS(INFO) << "Partially ignoring write to RTC operating mode (0x" << std::hex
                      << static_cast<uint32_t>(value) << ")";
      }
      break;

    case Register::kC:
      FX_LOGS(INFO) << "Ignoring write to read-only RTC flags (0x" << std::hex
                    << static_cast<uint32_t>(value) << ")";
      break;

    default:
      FX_LOGS(ERROR) << "Write to unsupported RTC register (0x" << std::hex
                     << static_cast<uint32_t>(reg) << ", 0x" << static_cast<uint32_t>(value) << ")";
      return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

void RtcMc146818::UpdateTime() {
  if (registers_[Register::kB] & kRegisterBStopTicks)
    return;

  time_t now = std::chrono::system_clock::to_time_t(Now() + offset_);
  tm datetime;
  gmtime_r(&now, &datetime);

  registers_[Register::kSeconds] = ToBcd(datetime.tm_sec);
  registers_[Register::kMinutes] = ToBcd(datetime.tm_min);
  registers_[Register::kHours] = ToBcd(datetime.tm_hour);
  registers_[Register::kDayOfWeek] = ToBcd(datetime.tm_wday + 1);
  registers_[Register::kDayOfMonth] = ToBcd(datetime.tm_mday);
  registers_[Register::kMonth] = ToBcd(datetime.tm_mon + 1);
  int year = datetime.tm_year + 1900;
  registers_[Register::kYear] = ToBcd(year % 100);
  registers_[Register::kCentury] = ToBcd(year / 100);
}

std::chrono::seconds RtcMc146818::GetOffset() {
  // Note: timegm() handles out-of-range values by just overflowing them to the
  // next higher unit (e.g., 70 seconds turns into 1 minute and 10 seconds).
  // Since the registers physically only allow for years 0000 to 9999, timegm()
  // will never return its only error EOVERFLOW since they all fit into a
  // 64-bit time_t
  tm datetime{
      .tm_sec = FromBcd(registers_[Register::kSeconds]),
      .tm_min = FromBcd(registers_[Register::kMinutes]),
      .tm_hour = FromBcd(registers_[Register::kHours]),
      .tm_mday = FromBcd(registers_[Register::kDayOfMonth]),
      .tm_mon = FromBcd(registers_[Register::kMonth]) - 1,
      .tm_year = FromBcd(registers_[Register::kYear]) +
                 100 * FromBcd(registers_[Register::kCentury]) - 1900,
  };
  auto offset = std::chrono::system_clock::from_time_t(timegm(&datetime)) - Now();
  return std::chrono::duration_cast<std::chrono::seconds>(offset);
}
