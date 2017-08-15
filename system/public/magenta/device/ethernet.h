// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>
#include <magenta/types.h>

// TODO: GET_DEVICE_INFO ioctl

// Get the 6 byte ethernet device MAC address
//   in: none
//   out: eth_info_t*
#define IOCTL_ETHERNET_GET_INFO \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_ETH, 0)

typedef struct eth_info_t {
    uint32_t features;
    uint32_t mtu;
    uint8_t mac[6];
    uint8_t pad[2];
    uint32_t reserved[12];
} eth_info_t;

#define ETH_SIGNAL_STATUS MX_USER_SIGNAL_0

// Ethernet device features

// Device is a wireless network device
#define ETH_FEATURE_WLAN  1
// Device is a synthetic network device
#define ETH_FEATURE_SYNTH 2

// Get the fifos to submit tx and rx operations
//   in: none
//  out: eth_fifos_t*
#define IOCTL_ETHERNET_GET_FIFOS \
    IOCTL(IOCTL_KIND_GET_TWO_HANDLES, IOCTL_FAMILY_ETH, 1)

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
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_ETH, 2)

// Start/Stop transferring packets
// Start will not succeed (MX_ERR_BAD_STATE) until the fifos have been
// obtained and an io buffer vmo has been registered.
#define IOCTL_ETHERNET_START \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_ETH, 3)
#define IOCTL_ETHERNET_STOP \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_ETH, 4)

// Receive all TX packets on this device looped back on RX path
#define IOCTL_ETHERNET_TX_LISTEN_START \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_ETH, 5)
#define IOCTL_ETHERNET_TX_LISTEN_STOP \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_ETH, 6)

// Associates a name with an ethernet instance.
#define IOCTL_ETHERNET_SET_CLIENT_NAME \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_ETH, 7)

// Returns: uint32_t link_status_bits
// The signal ETH_SIGNAL_STATUS will be asserted on rx_fifo when these bits change, and
// de-asserted when this ioctl is called.
#define IOCTL_ETHERNET_GET_STATUS \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_ETH, 8)

// Link status bits:
#define ETH_STATUS_ONLINE (1u)

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
#define ETH_FIFO_RX_TX   (4u)   // received our own tx packet (when TX_LISTEN)

typedef struct eth_fifo_entry {
    // offset from start of io_vmo to packet data
    uint32_t offset;
    // length of packet data
    uint16_t length;
    uint16_t flags;
    // opaque cookie
    void* cookie;
} eth_fifo_entry_t;

// ssize_t ioctl_ethernet_get_info(int fd, eth_info_t* out);
IOCTL_WRAPPER_OUT(ioctl_ethernet_get_info, IOCTL_ETHERNET_GET_INFO, eth_info_t);

// ssize_t ioctl_ethernet_get_fifos(int fd, eth_fifos_t* out);
IOCTL_WRAPPER_OUT(ioctl_ethernet_get_fifos, IOCTL_ETHERNET_GET_FIFOS, eth_fifos_t);

// ssize_t ioctl_ethernet_set_iobuf(int fd, mx_handle_t_t* entries);
IOCTL_WRAPPER_IN(ioctl_ethernet_set_iobuf, IOCTL_ETHERNET_SET_IOBUF, mx_handle_t);

// ssize_t ioctl_ethernet_start(int fd);
IOCTL_WRAPPER(ioctl_ethernet_start, IOCTL_ETHERNET_START);

// ssize_t ioctl_ethernet_stop(int fd);
IOCTL_WRAPPER(ioctl_ethernet_stop, IOCTL_ETHERNET_STOP);

// ssize_t ioctl_ethernet_tx_listen_start(int fd);
IOCTL_WRAPPER(ioctl_ethernet_tx_listen_start, IOCTL_ETHERNET_TX_LISTEN_START);

// ssize_t ioctl_ethernet_tx_listen_stop(int fd);
IOCTL_WRAPPER(ioctl_ethernet_tx_listen_stop, IOCTL_ETHERNET_TX_LISTEN_STOP);

// ssize_t ioctl_ethernet_set_client_name(int fd, const char* in, size_t in_len);
IOCTL_WRAPPER_VARIN(ioctl_ethernet_set_client_name, IOCTL_ETHERNET_SET_CLIENT_NAME, char);

// ssize_t ioctl_ethernet_get_status(int fd, uint32_t*);
IOCTL_WRAPPER_OUT(ioctl_ethernet_get_status, IOCTL_ETHERNET_GET_STATUS, uint32_t);
