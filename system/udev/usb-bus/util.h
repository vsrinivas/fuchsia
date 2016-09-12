// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>

mx_status_t usb_device_control(mx_device_t* hci_device, uint32_t device_id,
                               uint8_t request_type,  uint8_t request, uint16_t value,
                               uint16_t index, void* data, size_t length);

mx_status_t usb_device_get_descriptor(mx_device_t* hci_device, uint32_t device_id, uint16_t type,
                                      uint16_t index, void* data, size_t length);