// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>

#include <stdint.h>

#define IOCTL_ETHERTAP_CONFIG \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_ETHERTAP, 1)

#define ETHERTAP_MAX_NAME_LEN 31
#define ETHERTAP_MAX_MTU 2000

// Ethertap signals on the socket are used to indicate link status. It is an error to assert that a
// device is both online and offline; the device will be shutdown. A device is in the offline state
// when it is created.
// ZX_USER_SIGNAL_7 is reserved for internal ethertap use.
#define ETHERTAP_SIGNAL_ONLINE  ZX_USER_SIGNAL_0
#define ETHERTAP_SIGNAL_OFFLINE ZX_USER_SIGNAL_1

// Enables tracing of the ethertap device itself
#define ETHERTAP_OPT_TRACE         (1u << 0)
#define ETHERTAP_OPT_TRACE_PACKETS (1u << 1)
// Report EthmacSetParam() over Control channel of socket, and return success from EthmacSetParam().
// If this option is not set, EthmacSetParam() will return ZX_ERR_NOT_SUPPORTED.
#define ETHERTAP_OPT_REPORT_PARAM  (1u << 2)

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

// A header is prepended to socket communication from ethertap. This tells whether
// the subsequent bytes are a packet, a setparam report, etc.

#define ETHERTAP_MSG_PACKET (1u)
#define ETHERTAP_MSG_PARAM_REPORT (2u)

typedef struct ethertap_socket_header {
    uint32_t type;
    int32_t info; // Might not be used yet; also there for 64-bit alignment
} ethertap_socket_header_t;

// If EthmacSetParam() reporting is requested, this struct is written to the Control
// channel of the ethertap socket each time the function is called.
//
// CAUTION: The Control channel holds only one piece of data at a time. If EthmacSetParam()
// is called more than once without reading the struct, structs 2..N will be lost: consecutive
// calls of EthmacSetParam() without reading this struct will retain the very first result only.
// EthmacSetParam() will still return ZX_OK in that case, since it is a limitation of test
// infrastructure and not a simulated failure of the ethmac device under test.

#define SETPARAM_REPORT_DATA_SIZE 64

typedef struct ethertap_setparam_report {
    uint32_t param;
    int32_t value;
    // As needed for debug/test of individual params, the next two fields can be used
    // to return a hash or slice of the data sent in the ioctl data field.
    uint8_t data[SETPARAM_REPORT_DATA_SIZE];
    size_t data_length;
} ethertap_setparam_report_t;

// ssize_t ioctl_ethertap_config(int fd, const ethertap_ioctl_config_t* in, zx_handle_t* out);
IOCTL_WRAPPER_INOUT(ioctl_ethertap_config, IOCTL_ETHERTAP_CONFIG, \
        ethertap_ioctl_config_t, zx_handle_t);
