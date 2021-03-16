// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_NHLT_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_NHLT_H_

#include <lib/ddk/driver.h>

#include <acpica/acpi.h>

// Look for NHLT blob in the device pointed to by object and publish
// it as metadata on the PCI device.
// @param dev sys device pointer
// @param bbn base bus number of the PCI root the device is on
// @param adr ADR value for the device
// @param object handle to the device
zx_status_t nhlt_publish_metadata(zx_device_t* dev, uint8_t bbn, uint64_t adr, ACPI_HANDLE object);

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_NHLT_H_
