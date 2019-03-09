// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_DEVICE_QMI_TRANSPORT_H_
#define SYSROOT_ZIRCON_DEVICE_QMI_TRANSPORT_H_

// clang-format off

#include <stdint.h>
#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>

__BEGIN_CDECLS

// Get a channel handle for a two-way QMI channel for sending and
// receiving QMI requests/responses/indications
//   in: none
//   out: handle to channel
#define IOCTL_QMI_GET_CHANNEL \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_QMI, 0)

IOCTL_WRAPPER_OUT(ioctl_qmi_get_channel, IOCTL_QMI_GET_CHANNEL, zx_handle_t);

// Set the online status of the modem.
//   in: bool (connected or not)
//   out: none
#define IOCTL_QMI_SET_NETWORK \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_QMI, 1)

IOCTL_WRAPPER_IN(ioctl_qmi_set_network, IOCTL_QMI_SET_NETWORK, bool);

__END_CDECLS

#endif  // SYSROOT_ZIRCON_DEVICE_QMI_TRANSPORT_H_
