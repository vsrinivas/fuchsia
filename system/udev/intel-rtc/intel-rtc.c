// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <hw/inout.h>

#include <assert.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RTC_IO_BASE 0x70
#define RTC_NUM_IO_REGISTERS 8

#define RTC_IDX_REG 0x70
#define RTC_DATA_REG 0x71

enum intel_rtc_registers {
    REG_SECONDS,
    REG_SECONDS_ALARM,
    REG_MINUTES,
    REG_MINUTES_ALARM,
    REG_HOURS,
    REG_HOURS_ALARM,
    REG_DAY_OF_WEEK,
    REG_DAY_OF_MONTH,
    REG_MONTH,
    REG_YEAR,
};

struct rtc_time {
    uint8_t seconds, minutes, hours;
};
static void read_time(struct rtc_time* t) {
    outp(RTC_IDX_REG, REG_SECONDS);
    t->seconds = inp(RTC_DATA_REG);
    outp(RTC_IDX_REG, REG_MINUTES);
    t->minutes = inp(RTC_DATA_REG);
    outp(RTC_IDX_REG, REG_HOURS);
    t->hours = inp(RTC_DATA_REG);
}

static bool rtc_time_eq(struct rtc_time* lhs, struct rtc_time* rhs) {
    return lhs->seconds == rhs->seconds &&
           lhs->minutes == rhs->minutes &&
           lhs->hours == rhs->hours;
}

// implement char protocol
static ssize_t intel_rtc_read(mx_device_t* dev, void* buf, size_t count, mx_off_t off) {
    struct rtc_time cur, last;

    read_time(&cur);
    do {
        last = cur;
        read_time(&cur);
    } while (!rtc_time_eq(&cur, &last));

    int n = snprintf(buf, count, "%02x:%02x:%02x", cur.hours, cur.minutes, cur.seconds);
    if (n < 0) {
        return ERR_INTERNAL;
    }
    if ((unsigned int)n > count) {
        return ERR_BUFFER_TOO_SMALL;
    }
    return n;
}

static mx_protocol_device_t intel_rtc_device_proto = {
    .read = intel_rtc_read,
};

// implement driver object:
//
static mx_status_t intel_rtc_init(mx_driver_t* drv) {
#if defined(__x86_64__) || defined(__i386__)
    // TODO(teisenbe): This should be probed via the ACPI pseudo bus whenever it
    // exists.

    mx_status_t status = mx_mmap_device_io(get_root_resource(), RTC_IO_BASE, RTC_NUM_IO_REGISTERS);
    if (status != NO_ERROR) {
        return status;
    }

    mx_device_t* dev;
    status = device_create(&dev, drv, "rtc", &intel_rtc_device_proto);
    if (status != NO_ERROR) {
        return status;
    }

    status = device_add(dev, driver_get_misc_device());
    if (status != NO_ERROR) {
        free(dev);
        return status;
    }
    return NO_ERROR;
#else
    intel_rtc_device_proto = intel_rtc_device_proto;
    return ERR_NOT_SUPPORTED;
#endif
}

mx_driver_t _driver_intel_rtc = {
    .ops = {
        .init = intel_rtc_init,
    },
};

MAGENTA_DRIVER_BEGIN(_driver_intel_rtc, "intel-rtc", "magenta", "0.1", 0)
MAGENTA_DRIVER_END(_driver_intel_rtc)
