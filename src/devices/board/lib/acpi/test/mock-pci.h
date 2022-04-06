// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BOARD_LIB_ACPI_TEST_MOCK_PCI_H_
#define SRC_DEVICES_BOARD_LIB_ACPI_TEST_MOCK_PCI_H_

#include <fuchsia/hardware/pciroot/cpp/banjo.h>

#include <vector>

namespace acpi::test {

// Returns the list of pci_bdf_t passed to pci_init().
std::vector<pci_bdf_t> GetAcpiBdfs();
}  // namespace acpi::test

#endif  // SRC_DEVICES_BOARD_LIB_ACPI_TEST_MOCK_PCI_H_
