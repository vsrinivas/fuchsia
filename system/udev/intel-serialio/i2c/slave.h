// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <ddk/device.h>
#include <magenta/types.h>
#include <system/listnode.h>
#include <stdint.h>

typedef struct intel_serialio_i2c_slave_device {
    mx_device_t device;

    uint8_t chip_address_width;
    uint16_t chip_address;

    struct list_node slave_list_node;
} intel_serialio_i2c_slave_device_t;

mx_status_t intel_serialio_i2c_slave_device_init(
    mx_device_t* cont, intel_serialio_i2c_slave_device_t* slave,
    uint8_t width, uint16_t address);

#define get_intel_serialio_i2c_slave_device(dev) \
    containerof(dev, intel_serialio_i2c_slave_device_t, device)
