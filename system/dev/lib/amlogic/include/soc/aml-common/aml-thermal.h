// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/io-buffer.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/scpi.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <zircon/device/thermal.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <threads.h>

#define THERMAL_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define THERMAL_INFO(fmt, ...) zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)

#define MAX_TRIP_POINTS                 9

// GPIO Indexes
enum {
    FAN_CTL0,
    FAN_CTL1,
};

typedef struct {
    scpi_opp_t                          opps[MAX_DVFS_DOMAINS];

    uint32_t                            temp_sensor_id;

    uint32_t                            trip_point_count;
    thermal_temperature_info_t          trip_point_info[MAX_TRIP_POINTS];

    uint32_t                            critical_temp;

    bool                                active_cooling;
    bool                                passive_cooling;
    bool                                gpu_throttling;
} aml_thermal_config_t;

typedef struct {
    zx_device_t*                        zxdev;
    platform_device_protocol_t          pdev;

    gpio_protocol_t                     gpio;
    scpi_protocol_t                     scpi;

    zx_handle_t                         port;

    thrd_t                              notify_thread;

    aml_thermal_config_t                *device;

    uint32_t                            current_trip_idx;
    uint32_t                            current_temperature;
    uint32_t                            current_fan_level;
    uint32_t                            current_big_cluster_opp_idx;
    uint32_t                            current_little_cluster_opp_idx;
} aml_thermal_t;
