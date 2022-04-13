// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_LIB_ACPI_PCI_H_
#define SRC_DEVICES_BOARD_LIB_ACPI_PCI_H_
#include <fuchsia/hardware/pciroot/cpp/banjo.h>
#include <lib/ddk/device.h>

#include <vector>

#include <acpica/acpi.h>

#include "src/devices/board/lib/acpi/acpi.h"
#include "src/devices/board/lib/acpi/manager.h"

zx_status_t pci_init(zx_device_t* parent, ACPI_HANDLE object,
                     acpi::UniquePtr<ACPI_DEVICE_INFO> info, acpi::Manager* acpi,
                     std::vector<pci_bdf_t> acpi_bdfs);

#endif  // SRC_DEVICES_BOARD_LIB_ACPI_PCI_H_
