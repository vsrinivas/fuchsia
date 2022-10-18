// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_LIB_USB_INCLUDE_USB_PERIPHERAL_TEST_H_
#define SRC_DEVICES_USB_LIB_USB_INCLUDE_USB_PERIPHERAL_TEST_H_

// This file contains definitions for the USB Peripheral Test driver.
// The driver implements three endpoints: bulk out, bulk in and interrupt.
// The driver supports:
//
// 1. Writing data to the device with USB control request.
//
// 2. Reading data back from the device with USB control request.
//
// 3. Requesting the device to send an interrupt packet containing the data
//    sent via USB control request.
//
// 4. Looping back data via the two bulk endpoints.

// USB control request to write data to the device.
#define USB_PERIPHERAL_TEST_SET_DATA 1

// USB control request to read back data set by USB_PERIPHERAL_TEST_SET_DATA.
#define USB_PERIPHERAL_TEST_GET_DATA 2

// USB control request to request the device to send an interrupt request
// containing the data set via USB_PERIPHERAL_TEST_SET_DATA.
#define USB_PERIPHERAL_TEST_SEND_INTERUPT 3

#endif  // SRC_DEVICES_USB_LIB_USB_INCLUDE_USB_PERIPHERAL_TEST_H_
