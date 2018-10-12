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

#define I2C_CLASS_HID 1

#define IOCTL_I2C_SLAVE_TRANSFER \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_I2C, 3)

#define I2C_7BIT_ADDRESS 7
#define I2C_10BIT_ADDRESS 10

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

// ssize_t ioctl_i2c_slave_transfer(int fd, const i2c_slave_ioctl_segment_t* in, size_t in_len,
//                                  void* out, size_t out_len);
IOCTL_WRAPPER_VARIN_VAROUT(ioctl_i2c_slave_transfer, IOCTL_I2C_SLAVE_TRANSFER, i2c_slave_ioctl_segment_t, void);

__END_CDECLS;
