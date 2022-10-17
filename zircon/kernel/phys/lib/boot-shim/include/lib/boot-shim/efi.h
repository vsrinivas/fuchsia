// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_EFI_H_
#define ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_EFI_H_

#include <lib/zx/status.h>

#include <efi/system-table.h>
#include <efi/types.h>

#include "item-base.h"

// Forward declaration for <lib/acpi_lite.h>.
namespace acpi_lite {
class AcpiParser;
}  // namespace acpi_lite

namespace boot_shim {

// Look up the given GUID in the ConfigurionTable.
// Return it only if it matches the prefix.
const void* EfiGetVendorTable(const efi_system_table* systab, const efi_guid& guid,
                              std::string_view prefix = {});

// Create an AcpiParser for the ACPI tables found in the ConfigurionTable.
zx::result<acpi_lite::AcpiParser> EfiGetAcpi(const efi_system_table* systab);

// This just adds the ZBI_TYPE_EFI_SYSTEM_TABLE with the physical address.
class EfiSystemTableItem : public SingleOptionalItem<uint64_t, ZBI_TYPE_EFI_SYSTEM_TABLE> {
 public:
  void Init(const efi_system_table* systab);
};

// This just adds the ZBI_TYPE_SMBIOS with the physical address.
class EfiSmbiosItem : public SingleOptionalItem<uint64_t, ZBI_TYPE_SMBIOS> {
 public:
  void Init(const efi_system_table* systab);
};

}  // namespace boot_shim

#endif  // ZIRCON_KERNEL_PHYS_LIB_BOOT_SHIM_INCLUDE_LIB_BOOT_SHIM_EFI_H_
