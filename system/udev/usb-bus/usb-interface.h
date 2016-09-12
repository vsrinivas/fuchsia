// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <magenta/device/usb.h>

typedef struct usb_device usb_device_t;

mx_status_t usb_device_add_interface(usb_device_t* device,
                                     usb_device_descriptor_t* device_descriptor,
                                     usb_interface_descriptor_t* interface_desc,
                                     size_t interface_desc_length);

void usb_device_remove_interfaces(usb_device_t* device);
