// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

#include <stdint.h>
#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>

__BEGIN_CDECLS

// Get a channel handle for a two-way HCI command channel for sending and
// receiving HCI command and event packets, respectively.
//   in: none
//   out: handle to channel
#define IOCTL_BT_HCI_GET_COMMAND_CHANNEL \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_BT_HCI, 0)

// Get a uni-directional channel for listening to HCI command and event packet
// traffic. The format of each message is as follows:
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
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_BT_HCI, 1)

// Potential values for the flags bitfield in a snoop channel packet.
#define BT_HCI_SNOOP_FLAG_CMD      0x00
#define BT_HCI_SNOOP_FLAG_EVENT    0x01

// ssize_t ioctl_bt_hci_get_command_channel(int fd, mx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_bt_hci_get_command_channel, IOCTL_BT_HCI_GET_COMMAND_CHANNEL, mx_handle_t);

// ssize_t ioctl_bt_hci_get_snoop_channel(int fd, mx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_bt_hci_get_snoop_channel, IOCTL_BT_HCI_GET_SNOOP_CHANNEL, mx_handle_t);

// TODO(armansito): Add ioctls for ACL and SCO

__END_CDECLS
