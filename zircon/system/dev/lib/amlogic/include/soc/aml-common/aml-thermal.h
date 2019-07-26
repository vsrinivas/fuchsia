// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/io-buffer.h>
#include <ddk/metadata.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/scpi.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/device.h>
#include <fuchsia/hardware/thermal/c/fidl.h>
#include <threads.h>

#define THERMAL_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define THERMAL_INFO(fmt, ...) zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)

#define MAX_VOLTAGE_TABLE 31

// GPIO Indexes
enum {
  FAN_CTL0,
  FAN_CTL1,
  FAN_CTL_COUNT,
};

typedef struct {
  zx_device_t* zxdev;
  pdev_protocol_t pdev;

  gpio_protocol_t gpios[FAN_CTL_COUNT];
  scpi_protocol_t scpi;

  zx_handle_t port;

  thrd_t notify_thread;

  fuchsia_hardware_thermal_ThermalDeviceInfo* device;

  uint32_t temp_sensor_id;

  uint32_t current_trip_idx;
  uint32_t current_temperature;
  uint32_t current_fan_level;
  uint32_t current_big_cluster_opp_idx;
  uint32_t current_little_cluster_opp_idx;
} aml_thermal_t;

typedef struct {
  uint32_t microvolt;
  uint32_t duty_cycle;
} aml_voltage_table_t;

typedef struct {
  fuchsia_hardware_thermal_OperatingPointEntry opps[fuchsia_hardware_thermal_MAX_TRIP_POINTS];
  aml_voltage_table_t voltage_table[MAX_VOLTAGE_TABLE];
} aml_opp_info_t;
