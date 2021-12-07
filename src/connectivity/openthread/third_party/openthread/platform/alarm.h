// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_PLATFORM_ALARM_H_
#define SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_PLATFORM_ALARM_H_

#include <lib/ot-stack/ot-stack-callback.h>
#include <lib/zx/time.h>

extern "C" {
void platformAlarmInit(uint32_t speed_up_factor);
void platformAlarmProcess(otInstance *instance);
void platformAlarmUpdateTimeout(zx_time_t *timeout);
uint64_t otPlatTimeGet();
}

#endif  // SRC_CONNECTIVITY_OPENTHREAD_THIRD_PARTY_OPENTHREAD_PLATFORM_ALARM_H_
