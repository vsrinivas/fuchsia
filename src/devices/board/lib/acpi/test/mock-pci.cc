// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/board/lib/acpi/pci.h"

namespace acpi::test {
namespace {
std::vector<pci_bdf_t> stored_acpi_bdfs;

}

std::vector<pci_bdf_t> GetAcpiBdfs() { return std::move(stored_acpi_bdfs); }
}  // namespace acpi::test

zx_status_t pci_init(zx_device_t* platform_bus, ACPI_HANDLE object, ACPI_DEVICE_INFO* info,
                     acpi::Manager* acpi, std::vector<pci_bdf_t> acpi_bdfs) {
  acpi::test::stored_acpi_bdfs = std::move(acpi_bdfs);
  return ZX_OK;
}
