// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_THERMAL_H_
#define ZIRCON_SYSTEM_DEV_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_THERMAL_H_

#include <fuchsia/hardware/thermal/c/fidl.h>
#include <threads.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/scpi.h>

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
  aml_voltage_table_t voltage_table[MAX_VOLTAGE_TABLE];
} aml_voltage_table_info_t;

#endif  // ZIRCON_SYSTEM_DEV_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_THERMAL_H_
