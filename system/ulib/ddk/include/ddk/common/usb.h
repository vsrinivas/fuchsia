// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>

__BEGIN_CDECLS;

mx_status_t usb_control(mx_device_t* device, uint8_t request_type, uint8_t request,
                               uint16_t value, uint16_t index, void* data, size_t length);

mx_status_t usb_get_descriptor(mx_device_t* device, uint8_t request_type, uint16_t type,
                               uint16_t index, void* data, size_t length);

mx_status_t usb_get_status(mx_device_t* device, uint8_t request_type, uint16_t index,
                          void* data, size_t length);

mx_status_t usb_set_configuration(mx_device_t* device, int config);

mx_status_t usb_set_feature(mx_device_t* device, uint8_t request_type, int feature, int index);

mx_status_t usb_clear_feature(mx_device_t* device, uint8_t request_type, int feature, int index);

__END_CDECLS;
