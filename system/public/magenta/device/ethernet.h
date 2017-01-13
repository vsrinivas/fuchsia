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

// Get an eth_fifo_t structure for communicating with the ethernet device
//   in: eth_get_fifo_args_t
//   out: eth_fifo_t
#define IOCTL_ETHERNET_GET_FIFO \
    IOCTL(IOCTL_KIND_GET_THREE_HANDLES, IOCTL_FAMILY_ETH, 2)

// Set the VMO for rx and tx
//   in: mx_handle_t representing a VMO
//   out: none
#define IOCTL_ETHERNET_SET_IO_BUF \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_ETH, 3)

typedef struct eth_fifo_entry {
    uint32_t offset;
    uint16_t length;
    uint16_t flags;
    uint64_t cookie;
} eth_fifo_entry_t;

typedef struct eth_fifo {
    mx_handle_t entries_vmo;
    mx_handle_t rx_fifo;
    mx_handle_t tx_fifo;
    uint32_t version;
    uint32_t options;
    uint32_t rx_entries_count;
    uint32_t tx_entries_count;
} eth_fifo_t;

typedef struct eth_get_fifo_args {
    uint32_t options;
    uint32_t rx_entries;
    uint32_t tx_entries;
} eth_get_fifo_args_t;

typedef struct eth_set_io_buf_args {
    mx_handle_t io_buf_vmo;
    uint64_t rx_offset;
    size_t rx_len;
    uint64_t tx_offset;
    size_t tx_len;
} eth_set_io_buf_args_t;

// ssize_t ioctl_ethernet_get_mac_addr(int fd, uint8_t* out, size_t out_len);
IOCTL_WRAPPER_VAROUT(ioctl_ethernet_get_mac_addr, IOCTL_ETHERNET_GET_MAC_ADDR, uint8_t);

// ssize_t ioctl_ethernet_get_mtu(int fd, size_t* out);
IOCTL_WRAPPER_OUT(ioctl_ethernet_get_mtu, IOCTL_ETHERNET_GET_MTU, size_t);

// ssize_t ioctl_ethernet_get_fifo(int fd, eth_get_fifo_args_t* in, eth_fifo_t* out);
IOCTL_WRAPPER_INOUT(ioctl_ethernet_get_fifo, IOCTL_ETHERNET_GET_FIFO, eth_get_fifo_args_t, eth_fifo_t);

// ssize_t ioctl_ethernet_set_io_buf(int fd, eth_set_io_buf_args* in);
IOCTL_WRAPPER_IN(ioctl_ethernet_set_io_buf, IOCTL_ETHERNET_SET_IO_BUF, eth_set_io_buf_args_t);
