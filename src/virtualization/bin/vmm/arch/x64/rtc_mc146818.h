// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_RTC_MC146818_H_
#define SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_RTC_MC146818_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>

// Emulates the MC146818 real time clock, present in most PC BIOS's to
// track the wall time when systems are powered down
class RtcMc146818 {
 public:
  RtcMc146818();
  virtual ~RtcMc146818() = default;

  enum class Register : uint8_t {
    kSeconds = 0,
    kSecondsAlarm = 1,
    kMinutes = 2,
    kMinutesAlarm = 3,
    kHours = 4,
    kHoursAlarm = 5,
    kDayOfWeek = 6,
    kDayOfMonth = 7,
    kMonth = 8,
    kYear = 9,
    kA = 0xa,
    kB = 0xb,
    kC = 0xc,
    kCentury = 0x32,
  };
  constexpr static bool IsValidRegister(uint8_t reg) {
    return reg <= static_cast<uint8_t>(Register::kC) ||
           reg == static_cast<uint8_t>(Register::kCentury);
  }

  // Reads have side effects, so not const
  zx_status_t ReadRegister(Register reg, uint8_t* value);
  zx_status_t WriteRegister(Register reg, uint8_t value);

 protected:
  // Get the current system time. Virtual for testing use
  virtual std::chrono::time_point<std::chrono::system_clock> Now() const {
    return std::chrono::system_clock::now();
  }

 private:
  // Updates registers_ with the current time + offset_
  void UpdateTime() __TA_REQUIRES(mutex_);

  // Calculates the offset between the time in registers_ and the system clock:
  // <emulated time> = <real time> + offset_
  std::chrono::seconds GetOffset() __TA_REQUIRES(mutex_);

  mutable std::mutex mutex_;
  std::unordered_map<Register, uint8_t> __TA_GUARDED(mutex_) registers_;
  std::chrono::seconds __TA_GUARDED(mutex_) offset_{0};
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_ARCH_X64_RTC_MC146818_H_
