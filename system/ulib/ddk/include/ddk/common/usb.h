// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/protocol/usb-device.h>
#include <hw/usb.h>

__BEGIN_CDECLS;

mx_status_t usb_control(mx_device_t* device, uint8_t request_type, uint8_t request,
                               uint16_t value, uint16_t index, void* data, size_t length);

mx_status_t usb_get_descriptor(mx_device_t* device, uint8_t request_type, uint16_t type,
                               uint16_t index, void* data, size_t length);

// returns string to be freed with free() via out_string
mx_status_t usb_get_string_descriptor(mx_device_t* device, uint8_t id, char** out_string);

usb_speed_t usb_get_speed(mx_device_t* device);

mx_status_t usb_get_status(mx_device_t* device, uint8_t request_type, uint16_t index,
                          void* data, size_t length);

mx_status_t usb_set_configuration(mx_device_t* device, int config);

mx_status_t usb_set_feature(mx_device_t* device, uint8_t request_type, int feature, int index);

mx_status_t usb_clear_feature(mx_device_t* device, uint8_t request_type, int feature, int index);

// helper function for allocating iotxns for USB transfers
iotxn_t* usb_alloc_iotxn(usb_endpoint_descriptor_t* ep_desc, size_t data_size, size_t extra_size);

__END_CDECLS;
