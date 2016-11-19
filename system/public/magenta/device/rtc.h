// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>
#include <stdint.h>

__BEGIN_CDECLS;

typedef struct {
    uint8_t seconds, minutes, hours;
    uint8_t day, month;
    uint16_t year;
} rtc_t;

#define IOCTL_RTC_GET \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_RTC, 0)

#define IOCTL_RTC_SET \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_RTC, 1)

// ssize_t ioctl_rtc_get(int fd, rtc_t* rtc_out);
IOCTL_WRAPPER_OUT(ioctl_rtc_get, IOCTL_RTC_GET, rtc_t);

// ssize_t ioctl_rtc_set(int fd, const rtc_t* rtc_in);
IOCTL_WRAPPER_IN(ioctl_rtc_set, IOCTL_RTC_SET, rtc_t);

__END_CDECLS;
