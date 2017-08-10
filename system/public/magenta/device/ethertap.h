// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>

#include <stdint.h>

#define IOCTL_ETHERTAP_CONFIG \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_ETHERTAP, 1)

#define ETHERTAP_MAX_NAME_LEN 31

// Ethertap signals on the socket are used to indicate link status. It is an error to assert that a
// device is both online and offline; the device will be shutdown. A device is in the offline state
// when it is created.
#define ETHERTAP_SIGNAL_ONLINE  MX_USER_SIGNAL_0
#define ETHERTAP_SIGNAL_OFFLINE MX_USER_SIGNAL_1

// Enables tracing of the ethertap device itself
#define ETHERTAP_OPT_TRACE         (1u << 0)
#define ETHERTAP_OPT_TRACE_PACKETS (1u << 1)

// An ethertap device has a fixed mac address and mtu, and transfers ethernet frames over the
// returned data socket. To destroy the device, close the socket.
typedef struct ethertap_ioctl_config {
    // The name of this tap device.
    char name[ETHERTAP_MAX_NAME_LEN + 1];

    // Ethertap options (see above).
    uint32_t options;

    // Ethernet protocol fields for the ethermac device.
    uint32_t features;
    uint32_t mtu;
    uint8_t mac[6];
} ethertap_ioctl_config_t;

// ssize_t ioctl_ethertap_config(int fd, const ethertap_ioctl_config_t* in, mx_handle_t* out);
IOCTL_WRAPPER_INOUT(ioctl_ethertap_config, IOCTL_ETHERTAP_CONFIG, \
        ethertap_ioctl_config_t, mx_handle_t);
