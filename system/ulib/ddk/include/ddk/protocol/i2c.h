// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <ddk/ioctl.h>
#include <magenta/compiler.h>

__BEGIN_CDECLS;

#define IOCTL_I2C_BUS_ADD_SLAVE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_I2C, 0)
#define IOCTL_I2C_BUS_REMOVE_SLAVE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_I2C, 1)
#define IOCTL_I2C_BUS_SET_FREQUENCY \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_I2C, 2)
#define IOCTL_I2C_SLAVE_TRANSFER \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_I2C, 3)

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

__END_CDECLS;
