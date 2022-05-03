// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_LIB_ACPI_DEVICE_ARGS_H_
#define SRC_DEVICES_BOARD_LIB_ACPI_DEVICE_ARGS_H_

#include <lib/ddk/device.h>

#include <vector>

#include <acpica/acpi.h>

#include "src/devices/board/lib/acpi/manager.h"

#ifdef __Fuchsia__
#include <fuchsia/hardware/pciroot/cpp/banjo.h>
#else
using pci_bdf_t = struct pci_bdf {
  uint8_t bus_id;
  uint8_t device_id;
  uint8_t function_id;
};
#endif

namespace acpi {

inline const char* BusTypeToString(BusType t) {
  switch (t) {
    case kPci:
      return "pci";
    case kSpi:
      return "spi";
    case kI2c:
      return "i2c";
    case kUnknown:
      return "unknown";
  }
}

struct DeviceArgs {
  zx_device_t* parent_;
  acpi::Manager* manager_;
  ACPI_HANDLE handle_;

  // Bus metadata
  std::vector<uint8_t> metadata_;
  BusType bus_type_ = BusType::kUnknown;
  uint32_t bus_id_ = UINT32_MAX;

  // PCI metadata
  std::vector<pci_bdf_t> bdfs_;

  DeviceArgs(zx_device_t* parent, acpi::Manager* manager, ACPI_HANDLE handle)
      : parent_(parent), manager_(manager), handle_(handle) {}
  DeviceArgs(DeviceArgs&) = delete;

  DeviceArgs& SetBusMetadata(std::vector<uint8_t> metadata, BusType bus_type, uint32_t bus_id) {
    metadata_ = std::move(metadata);
    bus_type_ = bus_type;
    bus_id_ = bus_id;
    return *this;
  }
  DeviceArgs& SetPciMetadata(std::vector<pci_bdf_t> bdfs) {
    bdfs_ = std::move(bdfs);
    return *this;
  }
};

}  // namespace acpi
#endif
