// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_I2C_H_
#define SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_I2C_H_

#include <acpica/acpi.h>
#include <ddk/driver.h>

// Look at the metadata for an I2Cx device, find the address information for any
// children on the bus, and publish that metadata for a future I2C bus driver to
// retrieve later after its device is published by PCI.
zx_status_t I2cBusPublishMetadata(zx_device_t* dev, uint8_t pci_bus_num, uint64_t adr,
                                  const ACPI_DEVICE_INFO& i2c_bus_info, ACPI_HANDLE i2c_bus_object);

#endif  // SRC_DEVICES_BOARD_DRIVERS_X86_INCLUDE_I2C_H_
