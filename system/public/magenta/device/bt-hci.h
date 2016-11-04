// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

#include <stdint.h>
#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>

__BEGIN_CDECLS

// Get a message pipe handle for sending HCI commands and receiving HCI events
//   in: none
//   out: handle to message pipe
#define IOCTL_BT_HCI_GET_CONTROL_PIPE       IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BT_HCI, 0)

// Get a message pipe handle for sending and receiving ACL data
//   in: none
//   out: handle to message pipe
#define IOCTL_BT_HCI_GET_ACL_PIPE           IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BT_HCI, 1)

// ssize_t ioctl_bt_hci_get_control_pipe(int fd, mx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_bt_hci_get_control_pipe, IOCTL_BT_HCI_GET_CONTROL_PIPE, mx_handle_t);

// ssize_t ioctl_bt_hci_get_control_pipe(int fd, mx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_bt_hci_get_acl_pipe, IOCTL_BT_HCI_GET_ACL_PIPE, mx_handle_t);

__END_CDECLS
