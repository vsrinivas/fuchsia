// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

#include <stdint.h>
#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>

__BEGIN_CDECLS

// Potential values for the flags bitfield in a snoop channel packet.
typedef enum {
  BT_HCI_SNOOP_TYPE_CMD = 0,
  BT_HCI_SNOOP_TYPE_EVT = 1,
  BT_HCI_SNOOP_TYPE_ACL = 2,
  BT_HCI_SNOOP_TYPE_SCO = 3,
} bt_hci_snoop_type_t;

#define BT_HCI_SNOOP_FLAG_RECV 0x04 // Host -> Controller

static inline uint8_t bt_hci_snoop_flags(bt_hci_snoop_type_t type, bool is_received) {
  return (uint8_t)(type | (is_received ? BT_HCI_SNOOP_FLAG_RECV : 0x00));
}

// Get a channel handle for a two-way HCI command channel for sending and
// receiving HCI command and event packets, respectively.
//   in: none
//   out: handle to channel
#define IOCTL_BT_HCI_GET_COMMAND_CHANNEL \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_BT_HCI, 0)

// Get a channel handle for a two-way HCI ACL data channel for sending and receiving HCI ACL data
// packets.
//   in: none
//   out: handle to channel
#define IOCTL_BT_HCI_GET_ACL_DATA_CHANNEL \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_BT_HCI, 1)

// Get a uni-directional channel for sniffing HCI traffic. The format of each message is as follows:
//
//    [1-octet flags][n-octet payload]
//
// The flags octet is a bitfield with the following values defined:
//
//     - 0x00: The payload represents a command packet sent from the host to the
//             controller.
//     - 0x01: The payload represents an event packet sent by the controller.
//
// IOCTL parameters:
//   in: none
//   out: handle to channel
#define IOCTL_BT_HCI_GET_SNOOP_CHANNEL \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_BT_HCI, 2)

// ssize_t ioctl_bt_hci_get_command_channel(int fd, zx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_bt_hci_get_command_channel, IOCTL_BT_HCI_GET_COMMAND_CHANNEL, zx_handle_t);

// ssize_t ioctl_bt_hci_get_acl_data_channel(int fd, zx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_bt_hci_get_acl_data_channel, IOCTL_BT_HCI_GET_ACL_DATA_CHANNEL, zx_handle_t);

// ssize_t ioctl_bt_hci_get_snoop_channel(int fd, zx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_bt_hci_get_snoop_channel, IOCTL_BT_HCI_GET_SNOOP_CHANNEL, zx_handle_t);

// TODO(jamuraa): Add ioctl for SCO

__END_CDECLS
