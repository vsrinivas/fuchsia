// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <ddk/protocol/scpi.h>

#define MAX_TRIP_POINTS                      16
#define MAX_DVFS_DOMAINS                     2
// temperature units are in 10th of a degree kelvin
typedef struct {
    // state is a bitmask
    uint32_t state;
    // trip points for below states are defined below
#define THERMAL_STATE_NORMAL                 0
#define THERMAL_STATE_TRIP_VIOLATION         1
#define BIG_CLUSTER_POWER_DOMAIN             0
#define LITTLE_CLUSTER_POWER_DOMAIN          1

    // the sensor temperature at which the system should activate
    // passive cooling policy
    uint32_t passive_temp;

    // the sensor temperature at which the system should perform
    // critical shutdown
    uint32_t critical_temp;

    // number of trip points supported
    uint32_t max_trip_count;

    // the currently active trip point
    uint32_t active_trip[9];
} thermal_info_t;

typedef struct {
    uint32_t up_temp;
    uint32_t down_temp;
    int32_t fan_level;
    int32_t big_cluster_dvfs_opp;
    int32_t little_cluster_dvfs_opp;
    int32_t gpu_clk_freq_source;
} thermal_temperature_info_t;

typedef struct {
    // active cooling support
    bool active_cooling;

    // passive cooling support
    bool passive_cooling;

    // gpu throttling support
    bool gpu_throttling;

    // number of trip points
    uint32_t num_trip_points;

    // big-little architecture
    bool big_little;

    // critical temperature
    uint32_t critical_temp;

    // trip point information
    thermal_temperature_info_t trip_point_info[MAX_TRIP_POINTS];

    // opps information
    scpi_opp_t opps[MAX_DVFS_DOMAINS];
} thermal_device_info_t;

typedef struct {
    uint32_t id;
    uint32_t temp;
} trip_point_t;

typedef struct {
    uint16_t op_idx;
    uint32_t power_domain;
} dvfs_info_t;

// Get thermal info
#define IOCTL_THERMAL_GET_INFO \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_THERMAL, 1)

// Sets a trip point. When the sensor reaches the trip point temperature
// the device will notify on an event.
#define IOCTL_THERMAL_SET_TRIP \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_THERMAL, 2)

// Get an event to get trip point notifications on. ZX_USER_SIGNAL_0 is changed
// when either trip point is reached. It is deasserted when the state is read
// via IOCTL_THERMAL_GET_INFO.
#define IOCTL_THERMAL_GET_STATE_CHANGE_EVENT \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_THERMAL, 3)

// Get a port to get trip point notification packets.
#define IOCTL_THERMAL_GET_STATE_CHANGE_PORT \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_THERMAL, 4)

#define IOCTL_THERMAL_GET_DEVICE_INFO \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_THERMAL, 5)

#define IOCTL_THERMAL_SET_FAN_LEVEL \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_THERMAL, 6)

#define IOCTL_THERMAL_SET_DVFS_OPP \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_THERMAL, 7)

#define IOCTL_THERMAL_GET_TEMPERATURE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_THERMAL, 8)

#define IOCTL_THERMAL_GET_DVFS_INFO \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_THERMAL, 9)

#define IOCTL_THERMAL_GET_DVFS_OPP \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_THERMAL, 10)

#define IOCTL_THERMAL_GET_FAN_LEVEL \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_THERMAL, 11)

// ssize_t ioctl_thermal_get_info(int fd, thermal_info_t* out)
IOCTL_WRAPPER_OUT(ioctl_thermal_get_info, IOCTL_THERMAL_GET_INFO, thermal_info_t);

// ssize_t ioctl_thermal_set_trip(int fd, uint32_t temp)
IOCTL_WRAPPER_IN(ioctl_thermal_set_trip, IOCTL_THERMAL_SET_TRIP, trip_point_t);

// ssize_t ioctl_thermal_get_state_change_event(int fd, zx_handle_t* out)
IOCTL_WRAPPER_OUT(ioctl_thermal_get_state_change_event,
                  IOCTL_THERMAL_GET_STATE_CHANGE_EVENT,
                  zx_handle_t);

// ssize_t ioctl_thermal_get_state_change_port(int fd, zx_handle_t* out)
IOCTL_WRAPPER_OUT(ioctl_thermal_get_state_change_port,
                  IOCTL_THERMAL_GET_STATE_CHANGE_PORT,
                  zx_handle_t);

// ssize_t ioctl_thermal_get_device_info(int fd, thermal_info_t* out)
IOCTL_WRAPPER_OUT(ioctl_thermal_get_device_info,
                 IOCTL_THERMAL_GET_DEVICE_INFO, thermal_device_info_t);

// ssize_t ioctl_thermal_set_fan_level(int fd, uint32_t fan_level)
IOCTL_WRAPPER_IN(ioctl_thermal_set_fan_level, IOCTL_THERMAL_SET_FAN_LEVEL, int32_t);

// ssize_t ioctl_thermal_set_dvfs_opp(int fd, dvfs_info_t* info)
IOCTL_WRAPPER_IN(ioctl_thermal_set_dvfs_opp,
                 IOCTL_THERMAL_SET_DVFS_OPP, dvfs_info_t);

// ssize_t ioctl_thermal_get_temperature(int fd, uint32_t* temp)
IOCTL_WRAPPER_OUT(ioctl_thermal_get_temperature, IOCTL_THERMAL_GET_TEMPERATURE, uint32_t);

// ssize_t ioctl_thermal_get_dvfs_info(int fd, uint32_t* power_domain, scpi_opp_t* opp)
IOCTL_WRAPPER_INOUT(ioctl_thermal_get_dvfs_info, IOCTL_THERMAL_GET_DVFS_INFO,
                    uint32_t, scpi_opp_t);

// ssize_t ioctl_thermal_get_dvfs_opp(int fd, uint32_t* power_domain, uint32_t* opp)
IOCTL_WRAPPER_INOUT(ioctl_thermal_get_dvfs_opp, IOCTL_THERMAL_GET_DVFS_OPP,
                    uint32_t, uint32_t);

// ssize_t ioctl_thermal_get_fan_level(int fd, uint32_t* fan_level)
IOCTL_WRAPPER_OUT(ioctl_thermal_get_fan_level, IOCTL_THERMAL_GET_FAN_LEVEL, uint32_t);


