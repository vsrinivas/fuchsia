// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fuchsia_platform_alarm.h"

#include <lib/zx/time.h>

uint64_t FuchsiaPlatformAlarm::GetTimeMicroSec(void) {
  uint64_t cur_time_ns = static_cast<uint64_t>(zx_clock_get_monotonic());
  return (cur_time_ns / kNanoSecondsPerMicroSecond);
}

uint32_t FuchsiaPlatformAlarm::GetNowMicroSec(void) { return GetTimeMicroSec() * speed_up_factor_; }

uint32_t FuchsiaPlatformAlarm::GetNowMilliSec(void) {
  return GetNowMicroSec() / kMicroSecondsPerMilliSecond;
}

void FuchsiaPlatformAlarm::SetMilliSecAlarm(uint32_t time_ms) {
  is_ms_running_ = true;
  ms_alarm_ = time_ms;
}

void FuchsiaPlatformAlarm::ClearMilliSecAlarm() { is_ms_running_ = false; }

void FuchsiaPlatformAlarm::SetSpeedUpFactor(uint32_t speed_up_factor) {
  speed_up_factor_ = speed_up_factor;
}

uint32_t FuchsiaPlatformAlarm::GetRemainingTimeMicroSec() {
  int64_t remaining_time_us = INT32_MAX;
  uint32_t now = GetNowMicroSec();

  if (is_ms_running_) {
    int32_t remaining_time_ms =
        (ms_alarm_ - static_cast<uint32_t>(now / kMicroSecondsPerMilliSecond));
    if (remaining_time_ms <= 0) {
      // Note - code makes an assumption that we'll never set an
      // alarm which is more than INT32_MAX msec in future.
      // Which is true for practical purposes
      return 0;
    }
    remaining_time_us = remaining_time_ms * kMicroSecondsPerMilliSecond;
    remaining_time_us -= (now % kMicroSecondsPerMilliSecond);
  }

  if (is_us_running_) {
    int32_t usRemaining = (us_alarm_ - now);

    if (usRemaining < remaining_time_us) {
      remaining_time_us = usRemaining;
    }
  }

  remaining_time_us /= speed_up_factor_;

  if (remaining_time_us == 0) {
    remaining_time_us = 1;
  }

  return remaining_time_us;
}

bool FuchsiaPlatformAlarm::MilliSecAlarmFired() {
  int32_t remaining;
  bool alarm_fired = false;

  if (is_ms_running_) {
    remaining = (int32_t)(ms_alarm_ - GetNowMilliSec());

    if (remaining <= 0) {
      is_ms_running_ = false;
      alarm_fired = true;
    }
  }
  return alarm_fired;
}

bool FuchsiaPlatformAlarm::MicroSecAlarmFired() {
  bool alarm_fired = false;
  if (is_us_running_) {
    int32_t remaining = (int32_t)(us_alarm_ - GetNowMicroSec());

    if (remaining <= 0) {
      is_us_running_ = false;
      alarm_fired = true;
    }
  }
  return alarm_fired;
}

void FuchsiaPlatformAlarm::SetMicroSecAlarm(uint32_t time_us) {
  is_us_running_ = true;
  us_alarm_ = time_us;
}

void FuchsiaPlatformAlarm::ClearMicroSecAlarm() { is_us_running_ = false; }

uint32_t FuchsiaPlatformAlarm::MilliToMicroSec(uint32_t time_ms) {
  return (time_ms * kMicroSecondsPerMilliSecond);
}
uint32_t FuchsiaPlatformAlarm::MicroToMilliSec(uint32_t time_us) {
  return (time_us / kMicroSecondsPerMilliSecond);
}

void FuchsiaPlatformAlarm::SetOtStackCallBackPtr(OtStackCallBack* callback_ptr) {
  ot_stack_callback_ptr_ = callback_ptr;
}

OtStackCallBack* FuchsiaPlatformAlarm::GetOtStackCallBackPtr() { return ot_stack_callback_ptr_; }
