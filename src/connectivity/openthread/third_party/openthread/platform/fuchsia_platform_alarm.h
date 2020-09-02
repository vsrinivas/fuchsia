// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_PLATFORM_FUCHSIA_PLATFORM_ALARM_H_
#define SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_PLATFORM_FUCHSIA_PLATFORM_ALARM_H_

#include <lib/ot-stack/ot-stack-callback.h>
#include <stdint.h>

class FuchsiaPlatformAlarm {
 public:
  uint32_t GetNowMicroSec(void);
  uint32_t GetNowMilliSec(void);
  void SetMilliSecAlarm(uint32_t time_ms);
  void ClearMilliSecAlarm();
  void SetSpeedUpFactor(uint32_t speed_up_factor);

  uint32_t GetRemainingTimeMicroSec();

  bool MilliSecAlarmFired();

  static uint32_t MilliToMicroSec(uint32_t time_ms);
  static uint32_t MicroToMilliSec(uint32_t time_us);

  bool MicroSecAlarmFired();
  void SetMicroSecAlarm(uint32_t time_us);
  void ClearMicroSecAlarm();
  void SetOtStackCallBackPtr(OtStackCallBack *callback_ptr);
  OtStackCallBack *GetOtStackCallBackPtr();

 private:
  // Gets the current time in usec
  // Note - in posix world this is redefined in sim.
  // See if we need to do something similar
  static uint64_t GetTimeMicroSec(void);

  static constexpr uint64_t kNanoSecondsPerMicroSecond = 1000;
  static constexpr uint64_t kMicroSecondsPerMilliSecond = 1000;

  uint32_t speed_up_factor_ = 1;
  uint32_t ms_alarm_;
  bool is_ms_running_ = false;

  uint32_t us_alarm_;
  bool is_us_running_ = false;
  OtStackCallBack *ot_stack_callback_ptr_ = nullptr;
};

#endif  // SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_PLATFORM_FUCHSIA_PLATFORM_ALARM_H_
