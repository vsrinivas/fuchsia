// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>

#include <openthread/platform/alarm-micro.h>
#include <openthread/platform/alarm-milli.h>
#include <openthread/platform/diag.h>

#include "fuchsia_platform_alarm.h"

static FuchsiaPlatformAlarm alarm;

void platformAlarmInit(uint32_t speed_up_factor) { alarm.SetSpeedUpFactor(speed_up_factor); }

void platformAlarmSetCallbackPtr(OtStackCallBack *callback_ptr) {
  alarm.SetOtStackCallBackPtr(callback_ptr);
}

extern "C" uint32_t otPlatAlarmMilliGetNow(void) { return alarm.GetNowMilliSec(); }

extern "C" void otPlatAlarmMilliStartAt(otInstance *instance, uint32_t t0, uint32_t dt) {
  OT_UNUSED_VARIABLE(instance);

  alarm.SetMilliSecAlarm(t0 + dt);

  if (alarm.GetOtStackCallBackPtr()) {
    alarm.GetOtStackCallBackPtr()->PostDelayedAlarmTask(zx::duration(ZX_MSEC(dt)));
  }
}

extern "C" void otPlatAlarmMilliStop(otInstance *instance) {
  OT_UNUSED_VARIABLE(instance);
  alarm.ClearMilliSecAlarm();
}

#if OPENTHREAD_CONFIG_PLATFORM_USEC_TIMER_ENABLE
extern "C" uint32_t otPlatAlarmMicroGetNow(void) { return alarm.GetNowMicroSec(); }

extern "C" void otPlatAlarmMicroStartAt(otInstance *instance, uint32_t t0, uint32_t dt) {
  OT_UNUSED_VARIABLE(instance);

  alarm.SetMicroSecAlarm(t0 + dt);

  if (alarm.GetOtStackCallBackPtr()) {
    alarm.GetOtStackCallBackPtr()->PostDelayedAlarmTask(zx::duration(ZX_USEC(dt)));
  }
}

extern "C" void otPlatAlarmMicroStop(otInstance *instance) {
  OT_UNUSED_VARIABLE(instance);

  alarm.ClearMicroSecAlarm();
}
#endif  // OPENTHREAD_CONFIG_PLATFORM_USEC_TIMER_ENABLE

void platformAlarmUpdateTimeout(zx_time_t *timeout) {
  assert(timeout != NULL);
  *timeout = static_cast<zx_time_t>(alarm.GetRemainingTimeMicroSec());
}

void platformAlarmProcess(otInstance *instance) {
  if (alarm.MilliSecAlarmFired()) {
#if OPENTHREAD_CONFIG_DIAG_ENABLE
    if (otPlatDiagModeGet()) {
      otPlatDiagAlarmFired(instance);
    } else
#endif  // OPENTHREAD_CONFIG_DIAG_ENABLE
    {
      otPlatAlarmMilliFired(instance);
    }
  }

#if OPENTHREAD_CONFIG_PLATFORM_USEC_TIMER_ENABLE

  if (alarm.MicroSecAlarmFired()) {
    otPlatAlarmMicroFired(instance);
  }

#endif  // OPENTHREAD_CONFIG_PLATFORM_USEC_TIMER_ENABLE
}
