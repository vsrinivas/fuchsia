// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>
#include <magenta/types.h>

// Get the 6 byte ethernet device MAC address
//   in: none
//   out: uint8_t*
#define IOCTL_ETHERNET_GET_MAC_ADDR \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_ETH, 0)

// Get ethernet device MTU
//   in: none
//   out: size_t
#define IOCTL_ETHERNET_GET_MTU \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_ETH, 1)

// ssize_t ioctl_ethernet_get_mac_addr(int fd, uint8_t* out);
IOCTL_WRAPPER_OUT(ioctl_ethernet_get_mac_addr, IOCTL_ETHERNET_GET_MAC_ADDR, uint8_t);

// ssize_t ioctl_ethernet_get_mtu(int fd, size_t* out);
IOCTL_WRAPPER_OUT(ioctl_ethernet_get_mtu, IOCTL_ETHERNET_GET_MTU, size_t);
