// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_NET_RS_SRC_CPP_GUEST_ETHERNET_INTERFACE_H_
#define SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_NET_RS_SRC_CPP_GUEST_ETHERNET_INTERFACE_H_

#include <zircon/types.h>

#include "guest_ethernet.h"

extern "C" {
// Creation, initialization, and destruction functions for the C++ device.
zx_status_t guest_ethernet_create(GuestEthernet** guest_ethernet_out);
zx_status_t guest_ethernet_initialize(GuestEthernet* guest_ethernet,
                                      const void* rust_guest_ethernet, const uint8_t* mac,
                                      size_t mac_len, bool enable_bridge);
void guest_ethernet_destroy(GuestEthernet* guest_ethernet);

// Rust device -> C++ device interface.
zx_status_t guest_ethernet_send(GuestEthernet* guest_ethernet, void* data, uint16_t length);
void guest_ethernet_complete(GuestEthernet* guest_ethernet, uint32_t buffer_id, zx_status_t status);

// C++ device -> Rust device interface.
void guest_ethernet_set_status(const void* device, zx_status_t status);
void guest_ethernet_ready_for_tx(const void* device);
void guest_ethernet_receive_rx(const void* device, const uint8_t* data, size_t len,
                               uint32_t buffer_id);
}

#endif  // SRC_VIRTUALIZATION_BIN_VMM_DEVICE_VIRTIO_NET_RS_SRC_CPP_GUEST_ETHERNET_INTERFACE_H_
