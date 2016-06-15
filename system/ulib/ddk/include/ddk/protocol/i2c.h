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

#include <stdint.h>

enum {
    I2C_BUS_ADD_SLAVE = 0,
    I2C_BUS_REMOVE_SLAVE = 1,
    I2C_BUS_SET_FREQUENCY = 2,

    I2C_SLAVE_TRANSFER = 3,
};

enum {
    I2C_7BIT_ADDRESS = 7,
    I2C_10BIT_ADDRESS = 10,
};

typedef struct i2c_ioctl_add_slave_args {
    uint8_t chip_address_width;
    uint16_t chip_address;
} i2c_ioctl_add_slave_args_t;

typedef struct i2c_ioctl_remove_slave_args {
    uint8_t chip_address_width;
    uint16_t chip_address;
} i2c_ioctl_remove_slave_args_t;

typedef struct i2c_ioctl_set_bus_frequency_args {
    uint32_t frequency;
} i2c_ioctl_set_bus_frequency_args_t;

typedef struct i2c_slave_ioctl_segment {
    int read;
    int len;
    uint8_t buf[];
} i2c_slave_ioctl_segment_t;

typedef struct i2c_slave_segment {
    int read;
    int len;
    uint8_t* buf;
} i2c_slave_segment_t;
