/*
 *  Copyright (c) 2016, The OpenThread Authors.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the copyright holder nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <lib/zx/time.h>

#include <openthread/platform/alarm-micro.h>
#include <openthread/platform/alarm-milli.h>
#include <openthread/platform/diag.h>

#include "fuchsia_platform_alarm.h"

static FuchsiaPlatformAlarm alarm;

void platformAlarmInit(uint32_t speed_up_factor) { alarm.SetSpeedUpFactor(speed_up_factor); }

extern "C" uint32_t otPlatAlarmMilliGetNow(void) { return alarm.GetNowMilliSec(); }

extern "C" void otPlatAlarmMilliStartAt(otInstance *instance, uint32_t t0, uint32_t dt) {
  OT_UNUSED_VARIABLE(instance);

  alarm.SetMilliSecAlarm(t0 + dt);
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
