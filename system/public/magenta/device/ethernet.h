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

// Get an eth_ioring_t structure for communicating with the ethernet device
//   in: uint32_t (entry count)
//   out: eth_ioring_t
#define IOCTL_ETHERNET_GET_TX_IORING \
    IOCTL(IOCTL_KIND_GET_THREE_HANDLES, IOCTL_FAMILY_ETH, 2)
#define IOCTL_ETHERNET_GET_RX_IORING \
    IOCTL(IOCTL_KIND_GET_THREE_HANDLES, IOCTL_FAMILY_ETH, 3)

// Set the VMO for rx and tx
//   in: mx_handle_t representing a VMO
//   out: none
#define IOCTL_ETHERNET_SET_IOBUF \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_ETH, 4)

// Start transferring packets
#define IOCTL_ETHERNET_START \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_ETH, 5)

// Stop transferring packets
#define IOCTL_ETHERNET_STOP \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_ETH, 6)

// fifo entry flags
#define ETH_FIFO_RX_OK   (1u)
#define ETH_FIFO_TX_OK   (1u)
#define ETH_FIFO_INVALID (2u)

typedef struct eth_fifo_entry {
    uint32_t offset;
    uint16_t length;
    uint16_t flags;
    void* cookie;
} eth_fifo_entry_t;

typedef struct eth_ioring {
    // The entries array is large enough for 2 x entries.
    // First are the entries for enqueuing requests,
    // followed by the entries for dequeuing responses.
    mx_handle_t entries_vmo;
    mx_handle_t enqueue_fifo;
    mx_handle_t dequeue_fifo;
    uint32_t entries;
} eth_ioring_t;


// ssize_t ioctl_ethernet_get_mac_addr(int fd, uint8_t* out, size_t out_len);
IOCTL_WRAPPER_VAROUT(ioctl_ethernet_get_mac_addr, IOCTL_ETHERNET_GET_MAC_ADDR, uint8_t);

// ssize_t ioctl_ethernet_get_mtu(int fd, size_t* out);
IOCTL_WRAPPER_OUT(ioctl_ethernet_get_mtu, IOCTL_ETHERNET_GET_MTU, size_t);

// ssize_t ioctl_ethernet_get_tx_ioring(int fd, uint32_t* entries, eth_ioring_t* out);
IOCTL_WRAPPER_INOUT(ioctl_ethernet_get_tx_ioring, IOCTL_ETHERNET_GET_TX_IORING, uint32_t, eth_ioring_t);

// ssize_t ioctl_ethernet_get_rx_ioring(int fd, uint32_t* entries, eth_ioring_t* out);
IOCTL_WRAPPER_INOUT(ioctl_ethernet_get_rx_ioring, IOCTL_ETHERNET_GET_RX_IORING, uint32_t, eth_ioring_t);

// ssize_t ioctl_ethernet_set_io_buf(int fd, mx_handle_t* vmo);
IOCTL_WRAPPER_IN(ioctl_ethernet_set_iobuf, IOCTL_ETHERNET_SET_IOBUF, mx_handle_t);

// ssize_t ioctl_ethernet_start(int fd);
IOCTL_WRAPPER(ioctl_ethernet_start, IOCTL_ETHERNET_START);

// ssize_t ioctl_ethernet_stop(int fd);
IOCTL_WRAPPER(ioctl_ethernet_stop, IOCTL_ETHERNET_STOP);