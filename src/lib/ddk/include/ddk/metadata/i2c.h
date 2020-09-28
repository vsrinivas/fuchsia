// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_DDK_INCLUDE_DDK_METADATA_I2C_H_
#define SRC_LIB_DDK_INCLUDE_DDK_METADATA_I2C_H_

#include <stdint.h>

#include <ddk/binding.h>

#ifdef __cplusplus
#include <string.h>
#endif

typedef struct {
  uint32_t bus_id;
  uint16_t address;
  uint32_t i2c_class;
  // Used for binding directly to the I2C device using platform device IDs.
  // Set to zero if unused.
  uint32_t vid;
  uint32_t pid;
  uint32_t did;
} i2c_channel_t;

// A representation of I2C device metadata which exists in ACPI and is needed by
// the Intel I2C bus drivers.
//
// TODO(fxbug.dev/56832): Remove this when we have a better way to manage driver
// dependencies on ACPI.
#define ACPI_I2C_MAX_DEVPROPS 5
typedef struct acpi_i2c_device {
#ifdef __cplusplus
  acpi_i2c_device() { ::memset(this, 0, sizeof(*this)); }
#endif

  // i2c bus config
  uint8_t is_bus_controller;
  uint8_t ten_bit;
  uint16_t address;
  uint32_t bus_speed;
  // optional protocol id for this device
  uint32_t protocol_id;
  // optional additional device properties.
  zx_device_prop_t props[ACPI_I2C_MAX_DEVPROPS];
  uint32_t propcount;
} acpi_i2c_device_t;

#endif  // SRC_LIB_DDK_INCLUDE_DDK_METADATA_I2C_H_
