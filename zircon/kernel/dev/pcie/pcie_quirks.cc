// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <dev/pcie_bus_driver.h>

// This is a default empty table for pcie quirks on platforms that do not have
// any quirks to worry about. A const within the same translation unit as the
// extern definition results in the local initialized value being used, so to
// work around that the table lives here in a separate file.
extern __WEAK const PcieBusDriver::QuirkHandler pcie_quirk_handlers[] = {
    nullptr,
};
