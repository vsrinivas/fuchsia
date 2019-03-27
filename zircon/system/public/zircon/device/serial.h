// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_DEVICE_SERIAL_H_
#define SYSROOT_ZIRCON_DEVICE_SERIAL_H_

#include <zircon/compiler.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/device/ioctl.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// flags for serial_config()
#define SERIAL_DATA_BITS_5 (0 << 0)
#define SERIAL_DATA_BITS_6 (1 << 0)
#define SERIAL_DATA_BITS_7 (2 << 0)
#define SERIAL_DATA_BITS_8 (3 << 0)
#define SERIAL_DATA_BITS_MASK (3 << 0)

#define SERIAL_STOP_BITS_1 (0 << 2)
#define SERIAL_STOP_BITS_2 (1 << 2)
#define SERIAL_STOP_BITS_MASK (1 << 2)

#define SERIAL_PARITY_NONE (0 << 3)
#define SERIAL_PARITY_EVEN (1 << 3)
#define SERIAL_PARITY_ODD (2 << 3)
#define SERIAL_PARITY_MASK (3 << 3)

#define SERIAL_FLOW_CTRL_NONE (0 << 5)
#define SERIAL_FLOW_CTRL_CTS_RTS (1 << 5)
#define SERIAL_FLOW_CTRL_MASK (1 << 5)

// Set this flag to change baud rate but leave other properties unchanged
#define SERIAL_SET_BAUD_RATE_ONLY (1 << 31)

// serial port device class
#define SERIAL_CLASS_GENERIC 0
#define SERIAL_CLASS_BLUETOOTH_HCI 1
#define SERIAL_CLASS_CONSOLE 2

typedef struct {
    uint32_t baud_rate;
    uint32_t flags;
} serial_config_t;

// Sets the configuration for a serial device
#define IOCTL_SERIAL_CONFIG IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_SERIAL, 0)

#define IOCTL_SERIAL_GET_CLASS IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_SERIAL, 1)

IOCTL_WRAPPER_IN(ioctl_serial_config, IOCTL_SERIAL_CONFIG, serial_config_t)
IOCTL_WRAPPER_OUT(ioctl_serial_get_class, IOCTL_SERIAL_GET_CLASS, uint32_t)

__END_CDECLS

#endif  // SYSROOT_ZIRCON_DEVICE_SERIAL_H_
