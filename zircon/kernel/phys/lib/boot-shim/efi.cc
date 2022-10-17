// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lib/boot-shim/efi.h"

#include <lib/acpi_lite.h>
#include <lib/stdcompat/span.h>
#include <string.h>

#include <string_view>

#include <efi/system-table.h>
#include <efi/types.h>
#include <fbl/no_destructor.h>

namespace boot_shim {
namespace {

struct VendorTableMatch {
  efi_guid guid;
  std::string_view prefix;
};

const void* GetVendorTable(const efi_system_table* systab,
                           std::initializer_list<VendorTableMatch> matches) {
  cpp20::span config(systab->ConfigurationTable, systab->NumberOfTableEntries);
  for (const efi_configuration_table& table : config) {
    for (const auto& [guid, prefix] : matches) {
      if (!memcmp(&table.VendorGuid, &guid, sizeof(guid)) &&
          (prefix.empty() || !memcmp(table.VendorTable, prefix.data(), prefix.size()))) {
        return table.VendorTable;
      }
    }
  }
  return nullptr;
}

struct DirectPhysMemReader : public acpi_lite::PhysMemReader {
  zx::result<const void*> PhysToPtr(uintptr_t paddr, size_t bytes) override {
    return zx::success(reinterpret_cast<const void*>(paddr));
  }
};

}  // namespace

void EfiSystemTableItem::Init(const efi_system_table* systab) {
  set_payload(reinterpret_cast<uintptr_t>(systab));
}

const void* EfiGetVendorTable(const efi_system_table* systab, const efi_guid& guid,
                              std::string_view prefix) {
  return GetVendorTable(systab, {{guid, prefix}});
}

void EfiSmbiosItem::Init(const efi_system_table* systab) {
  if (const void* table = GetVendorTable(systab,  //
                                         {
                                             {SMBIOS_TABLE_GUID, "_SM_"},
                                             {SMBIOS3_TABLE_GUID, "_SM3_"},
                                         })) {
    set_payload(reinterpret_cast<uintptr_t>(table));
  }
}

zx::result<acpi_lite::AcpiParser> EfiGetAcpi(const efi_system_table* systab) {
  constexpr std::string_view kRsdPtr = "RSD PTR ";
  if (const void* table = GetVendorTable(systab,  //
                                         {
                                             {ACPI_TABLE_GUID, kRsdPtr},
                                             {ACPI_20_TABLE_GUID, kRsdPtr},
                                         })) {
    static fbl::NoDestructor<DirectPhysMemReader> phys_mem_reader;
    zx_paddr_t paddr = reinterpret_cast<uintptr_t>(table);
    return acpi_lite::AcpiParser::Init(*phys_mem_reader, paddr);
  }
  return zx::error(ZX_ERR_NOT_FOUND);
}

}  // namespace boot_shim
