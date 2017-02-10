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

// Get the fifos to submit tx and rx operations
//   in: none
//  out: eth_fifos_t
#define IOCTL_ETHERNET_GET_FIFOS \
    IOCTL(IOCTL_KIND_GET_TWO_HANDLES, IOCTL_FAMILY_ETH, 2)

typedef struct eth_fifos_t {
    // handles to tx and rx fifos
    mx_handle_t tx_fifo;
    mx_handle_t rx_fifo;
    // maximum number of items in fifos
    uint32_t tx_depth;
    uint32_t rx_depth;
} eth_fifos_t;

// Set the io buffer that tx and rx operations act on
//   in: mx_handle_t (vmo)
//  out: none
#define IOCTL_ETHERNET_SET_IOBUF \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_ETH, 3)

// Start transferring packets
// Start will not succeed (ERR_BAD_STATE) until the fifos have been
// obtained and an io buffer vmo has been registered.
#define IOCTL_ETHERNET_START \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_ETH, 4)

// Stop transferring packets
#define IOCTL_ETHERNET_STOP \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_ETH, 5)


// Operation
//
// Packets are transmitted by writing data into the io_vmo and writing
// an eth_fifo_entry_t referencing that data (offset + length) into the
// tx_fifo.  When the driver is done accessing the data, an eth_fifo_entry_t
// with the same cookie value (opaque to the driver) will be readable
// from the tx fifo.
//
// Packets are received by writing an eth_fifo_entry_t referencing an
// available buffer (offset + length) in the io_vmo.  When a packet is
// received, an eth_fifo_entry_t with the same cookie value (opaque to
// the driver) will be readable from the rx fifo.  The offset field will
// be the same as was sent.  The length field will reflect the actual size
// of the received packet.  The flags field will indicate success or a
// specific failure condition.
//
// IMPORTANT: The driver *will not* buffer response messages.  It is the
// client's responsibility to ensure that there is space in the reply side
// of each fifo for each outstanding tx or rx request.  The fifo sizes
// are returned along with the fifo handles in the eth_fifos_t.

// flags values for request messages
// - none -

// flags values for response messages
#define ETH_FIFO_RX_OK   (1u)   // packet received okay
#define ETH_FIFO_TX_OK   (1u)   // packet transmitted okay
#define ETH_FIFO_INVALID (2u)   // offset+length not within io_vmo bounds

typedef struct eth_fifo_entry {
    // offset from start of io_vmo to packet data
    uint32_t offset;
    // length of packet data
    uint16_t length;
    uint16_t flags;
    // opaque cookie
    void* cookie;
} eth_fifo_entry_t;


// ssize_t ioctl_ethernet_get_mac_addr(int fd, uint8_t* out, size_t out_len);
IOCTL_WRAPPER_VAROUT(ioctl_ethernet_get_mac_addr, IOCTL_ETHERNET_GET_MAC_ADDR, uint8_t);

// ssize_t ioctl_ethernet_get_mtu(int fd, size_t* out);
IOCTL_WRAPPER_OUT(ioctl_ethernet_get_mtu, IOCTL_ETHERNET_GET_MTU, size_t);

// ssize_t ioctl_ethernet_get_fifos(int fd, eth_fifos_t* fifos);
IOCTL_WRAPPER_OUT(ioctl_ethernet_get_fifos, IOCTL_ETHERNET_GET_FIFOS, eth_fifos_t);

// ssize_t ioctl_ethernet_set_iobuf(int fd, mx_handle_t_t* entries);
IOCTL_WRAPPER_IN(ioctl_ethernet_set_iobuf, IOCTL_ETHERNET_SET_IOBUF, mx_handle_t);

// ssize_t ioctl_ethernet_start(int fd);
IOCTL_WRAPPER(ioctl_ethernet_start, IOCTL_ETHERNET_START);

// ssize_t ioctl_ethernet_stop(int fd);
IOCTL_WRAPPER(ioctl_ethernet_stop, IOCTL_ETHERNET_STOP);