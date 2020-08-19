// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon/system/utest/device-enumeration/aemu.h"

#include <errno.h>
#include <fcntl.h>
#include <fuchsia/hardware/acpi/llcpp/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <string.h>
#include <unistd.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <cstdlib>
#include <cstring>
#include <string_view>

#include <fbl/array.h>
#include <fbl/span.h>
#include <fbl/unique_fd.h>

namespace device_enumeration {

namespace {

using llcpp::fuchsia::hardware::acpi::Acpi;
using llcpp::fuchsia::hardware::acpi::TableInfo;

const char* kAcpiDevicePath = "/dev/sys/platform/acpi";
const char* kAcpiDsdtTableName = "DSDT";

template <typename T>
bool FindPattern(const fbl::Array<T>& haystack, const fbl::Array<T>& needle) {
  if (needle.size() > haystack.size()) {
    return false;
  }
  for (size_t start = 0; start + needle.size() <= haystack.size(); ++start) {
    if (memcmp(needle.data(), haystack.data() + start, needle.size()) == 0) {
      return true;
    }
  }
  return false;
}

// Fetch raw data for a table.
zx_status_t FetchTable(const zx::channel& channel, const TableInfo& table,
                       fbl::Array<uint8_t>* data) {
  // Allocate a VMO for the read.
  zx::vmo vmo;
  if (zx_status_t status = zx::vmo::create(table.size, /*options=*/0, &vmo); status != ZX_OK) {
    return status;
  }

  // Make a copy to send to the driver.
  zx::vmo vmo_copy;
  if (zx_status_t status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo_copy); status != ZX_OK) {
    return status;
  }

  // Fetch the data.
  Acpi::ResultOf::ReadNamedTable result =
      Acpi::Call::ReadNamedTable(channel.borrow(), table.name, 0, std::move(vmo_copy));
  if (!result.ok()) {
    return result.status();
  }

  // Copy the data into memory.
  uint32_t size = result.value().result.response().size;
  auto table_data = fbl::Array<uint8_t>(new uint8_t[size], size);
  if (zx_status_t status = vmo.read(table_data.data(), 0, size); status != ZX_OK) {
    return status;
  }

  *data = std::move(table_data);
  return ZX_OK;
}

// Find a certain byte sequence |keyword| in ACPI table |table_name|.
//
// Returns false if it cannot access the ACPI data, or none of the ACPI
// tables with name |table_name| has the keyword.
bool AcpiTableHasKeyword(const zx::channel& acpi_channel, std::string_view table_name,
                         const fbl::Array<uint8_t>& keyword) {
  // List ACPI entries.
  Acpi::ResultOf::ListTableEntries result =
      Acpi::Call::ListTableEntries(zx::unowned_channel(acpi_channel));
  if (!result.ok()) {
    fprintf(stderr, "Could not list ACPI table entries: %s.\n",
            zx_status_get_string(result.status()));
    return false;
  }
  if (result.value().result.is_err()) {
    fprintf(stderr, "Call to list ACPI table entries failed: %s.\n",
            zx_status_get_string(result.value().result.err()));
    return false;
  }

  auto& entries = result.value().result.response().entries;
  for (auto table : entries) {
    if (std::string_view(reinterpret_cast<const char*>(table.name.begin()), table.name.size()) !=
        table_name) {
      continue;
    }

    // Fetch table contents.
    fbl::Array<uint8_t> table_data;
    zx_status_t status = FetchTable(acpi_channel, table, &table_data);
    if (status != ZX_OK) {
      fprintf(stderr, "Call to FetchTable failed: %s.\n", zx_status_get_string(status));
      continue;
    }

    // There can be multiple tables with the same name. So we continue
    // searching for the keyword if it is missing in the current table.
    if (FindPattern(table_data, keyword)) {
      return true;
    }
  }
  return false;
}

}  // namespace

// AEMU and QEMU boards have the same board name, but AEMU boards also have
// some AEMU-specific ACPI devices which can be used for AEMU board detection.
//
// In this function we find Goldfish pipe device, with ACPI HID |GFSH0002|,
// in the ACPI DSDT (Differentiated System Description Table) table. This
// device can be found if and only if it's an AEMU board.
bool IsAemuBoard() {
  // Open up channel to ACPI device.
  fbl::unique_fd fd(open(kAcpiDevicePath, O_RDWR));
  if (!fd.is_valid()) {
    return false;
  }

  zx::channel channel;
  if (fdio_get_service_handle(fd.release(), channel.reset_and_get_address()) != ZX_OK) {
    return false;
  }

  // Look for |GFSH0002| in ACPI table.
  constexpr size_t kAemuAcpiKeywordLen = 8;
  constexpr uint8_t kAemuAcpiKeyword[] = "GFSH0002";
  fbl::Array<uint8_t> aemu_acpi_keyword(new uint8_t[kAemuAcpiKeywordLen], kAemuAcpiKeywordLen);
  memcpy(aemu_acpi_keyword.data(), kAemuAcpiKeyword, kAemuAcpiKeywordLen);

  return AcpiTableHasKeyword(channel, kAcpiDsdtTableName, aemu_acpi_keyword);
}

}  // namespace device_enumeration
