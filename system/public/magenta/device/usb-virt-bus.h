// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

#include <stdint.h>
#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>

__BEGIN_CDECLS

// enables or disables the virtual bus
// call with in_len = sizeof(int)
#define IOCTL_USB_VIRT_BUS_ENABLE           IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB_VIRT_BUS, 0)

// sets the device controller's connected state
// call with in_len = sizeof(int)
#define IOCTL_USB_VIRT_BUS_SET_CONNECTED    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB_VIRT_BUS, 1)

IOCTL_WRAPPER_IN(ioctl_usb_virt_bus_enable, IOCTL_USB_VIRT_BUS_ENABLE, int);
IOCTL_WRAPPER_IN(ioctl_usb_virt_bus_set_connected, IOCTL_USB_VIRT_BUS_SET_CONNECTED, int);

__END_CDECLS
