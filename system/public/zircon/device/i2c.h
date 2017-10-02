// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

#define IOCTL_I2C_BUS_ADD_SLAVE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_I2C, 0)
#define IOCTL_I2C_BUS_REMOVE_SLAVE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_I2C, 1)
#define IOCTL_I2C_BUS_SET_FREQUENCY \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_I2C, 2)
#define IOCTL_I2C_SLAVE_TRANSFER \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_I2C, 3)
#define IOCTL_I2C_SLAVE_IRQ \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_I2C, 4)

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

#define I2C_SEGMENT_TYPE_END   0
#define I2C_SEGMENT_TYPE_READ  1
#define I2C_SEGMENT_TYPE_WRITE 2
typedef struct i2c_slave_ioctl_segment {
    int type;
    int len;
} i2c_slave_ioctl_segment_t;

typedef struct i2c_slave_segment {
    int type;
    int len;
    uint8_t* buf;
} i2c_slave_segment_t;

// ssize_t ioctl_i2c_bus_add_slave(int fd, const i2c_ioctl_add_slave_args_t* in);
IOCTL_WRAPPER_IN(ioctl_i2c_bus_add_slave, IOCTL_I2C_BUS_ADD_SLAVE, i2c_ioctl_add_slave_args_t);

// ssize_t ioctl_i2c_bus_remove_slave(int fd, const i2c_ioctl_remove_slave_args_t* in);
IOCTL_WRAPPER_IN(ioctl_i2c_bus_remove_slave, IOCTL_I2C_BUS_REMOVE_SLAVE, i2c_ioctl_remove_slave_args_t);

// ssize_t ioctl_i2c_bus_set_frequency(int fd, const i2c_ioctl_set_bus_frequency_args_t* in);
IOCTL_WRAPPER_IN(ioctl_i2c_bus_set_frequency, IOCTL_I2C_BUS_SET_FREQUENCY, i2c_ioctl_set_bus_frequency_args_t);

// ssize_t ioctl_i2c_slave_transfer(int fd, const i2c_slave_ioctl_segment_t* in, size_t in_len,
//                                  void* out, size_t out_len);
IOCTL_WRAPPER_VARIN_VAROUT(ioctl_i2c_slave_transfer, IOCTL_I2C_SLAVE_TRANSFER, i2c_slave_ioctl_segment_t, void);

// ssize_t ioctl_i2c_slave_irq(int fd, zx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_i2c_slave_irq, IOCTL_I2C_SLAVE_IRQ, zx_handle_t);

__END_CDECLS;
