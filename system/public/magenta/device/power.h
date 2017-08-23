// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>

typedef struct {
    uint32_t type;
#define POWER_TYPE_AC      0
#define POWER_TYPE_BATTERY 1

    uint32_t state;
    // state is a bitmask
#define POWER_STATE_ONLINE      (1 << 0)
    // online means the power source is online for POWER_TYPE_AC and the battery
    // is present for POWER_TYPE_BATTERY

    // below 3 states are only valid for POWER_TYPE_BATTERY
    // a battery may be discharging, charging, or neither discharging nor charging.
    // it is illegal for both POWER_STATE_DISCHARGING and POWER_STATE_CHARGING
    // to be set. POWER_STATE_CRITICAL is set when the battery reaches an
    // OEM-defined critical level and the system should perform a shutdown.
#define POWER_STATE_DISCHARGING (1 << 1)
#define POWER_STATE_CHARGING    (1 << 2)
#define POWER_STATE_CRITICAL    (1 << 3)
} power_info_t;

// The remaining battery percentage is calculated using the following formula:
// remaining battery percentage [%] = remaining_capacity / last_full_capacity * 100
//
// The remaining battery life is calculated using the following formula:
// remaining_battery_life [h] = remaining_capacity / present_rate

typedef struct {
    uint32_t unit;
    // capacity unit. all voltage fields are in millivolts
#define BATTERY_UNIT_MW 0
#define BATTERY_UNIT_MA 1

    uint32_t design_capacity;
    // nominal capacity of a new battery
    uint32_t last_full_capacity;
    // predicted battery capacity when fully charged
    uint32_t design_voltage;
    // nominal voltage of a new battery
    uint32_t capacity_warning;
    // capacity when the device will generate a warning notification
    uint32_t capacity_low;
    // capacity when the device will generate a low battery notification
    uint32_t capacity_granularity_low_warning;
    // the smallest increment the battery is capable of measuring between the
    // low and warning capacities
    uint32_t capacity_granularity_warning_full;
    // the smallest increment the battery is capable of measuring between the low
    // and warning capacities

    // below fields are in units specified in battery_info_t
    int32_t present_rate;
    // charging/discharging rate in the capacity unit. positive is charging,
    // negative is discharging
    uint32_t remaining_capacity;
    uint32_t present_voltage;
} battery_info_t;

// Get device info
#define IOCTL_POWER_GET_INFO \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_POWER, 1)

// Get battery info. Only supported if type == POWER_TYPE_BATTERY
#define IOCTL_POWER_GET_BATTERY_INFO \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_POWER, 2)

// Get an event to get state change notifications on. MX_SIGNAL_USER_0 is
// asserted when power_info_t.state is changed. It is deasserted when the
// state is read via IOCTL_POWER_GET_INFO.
#define IOCTL_POWER_GET_STATE_CHANGE_EVENT \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_POWER, 3)

// ssize_t ioctl_power_get_info(int fd, power_info_t* out)
IOCTL_WRAPPER_OUT(ioctl_power_get_info, IOCTL_POWER_GET_INFO, power_info_t);

// ssize_t ioctl_power_get_battery_info(int fd, battery_info_t* out)
IOCTL_WRAPPER_OUT(ioctl_power_get_battery_info,
                  IOCTL_POWER_GET_BATTERY_INFO,
                  battery_info_t);

// ssize_t ioctl_power_get_state_change_event(int fd, mx_handle_t* out)
IOCTL_WRAPPER_OUT(ioctl_power_get_state_change_event,
                  IOCTL_POWER_GET_STATE_CHANGE_EVENT,
                  mx_handle_t);
