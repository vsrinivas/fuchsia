// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_I2C_INTEL_I2C_INTEL_I2C_SUBORDINATE_H_
#define ZIRCON_SYSTEM_DEV_I2C_INTEL_I2C_INTEL_I2C_SUBORDINATE_H_

#include <stdint.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include <ddk/binding.h>
#include <ddk/device.h>

#define I2C_7BIT_ADDRESS 7
#define I2C_10BIT_ADDRESS 10

typedef struct i2c_subordinate_segment {
  int type;
  int len;
  uint8_t* buf;
} i2c_subordinate_segment_t;

typedef struct intel_serialio_i2c_subordinate_device {
  zx_device_t* zxdev;
  struct intel_serialio_i2c_device* controller;

  uint8_t chip_address_width;
  uint16_t chip_address;

  struct list_node subordinate_list_node;
} intel_serialio_i2c_subordinate_device_t;

// device protocol for a subordinate device
extern zx_protocol_device_t intel_serialio_i2c_subordinate_device_proto;

zx_status_t intel_serialio_i2c_subordinate_transfer(
    intel_serialio_i2c_subordinate_device_t* subordinate, i2c_subordinate_segment_t* segments,
    int segment_count);
zx_status_t intel_serialio_i2c_subordinate_get_irq(
    intel_serialio_i2c_subordinate_device_t* subordinate, zx_handle_t* out);

#endif  // ZIRCON_SYSTEM_DEV_I2C_INTEL_I2C_INTEL_I2C_SUBORDINATE_H_
