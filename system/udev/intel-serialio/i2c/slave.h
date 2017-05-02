// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/binding.h>
#include <ddk/device.h>
#include <magenta/types.h>
#include <magenta/listnode.h>
#include <stdint.h>

typedef struct intel_serialio_i2c_slave_device {
    mx_device_t* mxdev;
    struct intel_serialio_i2c_device* controller;

    uint8_t chip_address_width;
    uint16_t chip_address;

    mx_device_prop_t props[3];

    struct list_node slave_list_node;
} intel_serialio_i2c_slave_device_t;

// device protocol for a slave device
extern mx_protocol_device_t intel_serialio_i2c_slave_device_proto;
