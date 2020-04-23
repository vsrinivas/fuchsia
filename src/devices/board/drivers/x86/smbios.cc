// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "smbios.h"

#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/smbios/smbios.h>
#include <lib/zx/resource.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <limits.h>

#include <ddk/driver.h>
#include <fbl/algorithm.h>

namespace {

// Map the structure with the given physical address and length.  Neither needs
// to be page-aligned.
zx_status_t MapStructure(const zx::resource& resource, zx_paddr_t paddr, size_t length,
                         fzl::OwnedVmoMapper* mapping, uintptr_t* offsetted_start) {
  zx_paddr_t base_paddr = fbl::round_down<zx_paddr_t>(paddr, static_cast<zx_paddr_t>(PAGE_SIZE));
  zx::vmo vmo;
  size_t page_offset = paddr - base_paddr;
  size_t mapping_size = fbl::round_up<size_t>(length + page_offset, static_cast<size_t>(PAGE_SIZE));
  zx_status_t status = zx::vmo::create_physical(resource, base_paddr, mapping_size, &vmo);
  if (status != ZX_OK) {
    return status;
  }

  fzl::OwnedVmoMapper new_mapping;
  status = new_mapping.Map(std::move(vmo), mapping_size, ZX_VM_PERM_READ);
  if (status != ZX_OK) {
    return status;
  }
  *mapping = std::move(new_mapping);
  *offsetted_start = reinterpret_cast<uintptr_t>(mapping->start()) + page_offset;
  return ZX_OK;
}

// Structure for handling the lifetime of the SMBIOS mappings
class SmbiosState {
 public:
  SmbiosState() = default;
  ~SmbiosState() = default;

  // Must only be invoked once on an instance.  On success, |entry_point()|
  // and |struct_table_mapping()| are usable.  |entry_point()| will be
  // guaranteed to be a valid SMBIOS entry point structure.
  zx_status_t LoadFromFirmware();

  // These values are only valid as long as the instance is around.
  const smbios::EntryPoint2_1* entry_point() const { return entry_point_; }
  uintptr_t struct_table_start() const { return struct_table_start_; }

 private:
  fzl::OwnedVmoMapper entry_point_mapping_;
  fzl::OwnedVmoMapper struct_table_mapping_;

  const smbios::EntryPoint2_1* entry_point_ = nullptr;
  uintptr_t struct_table_start_ = 0;
};

zx_status_t SmbiosState::LoadFromFirmware() {
  // Please do not use get_root_resource() in new code. See ZX-1467.
  zx::unowned_resource root_resource(get_root_resource());

  zx_paddr_t acpi_rsdp, smbios_ep;
  zx_status_t status = zx_pc_firmware_tables(root_resource->get(), &acpi_rsdp, &smbios_ep);
  if (status != ZX_OK) {
    return status;
  }

  if (smbios_ep == 0) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Map the entry point and see how much data we have
  fzl::OwnedVmoMapper ep_mapping;
  uintptr_t ep_start;
  status = MapStructure(*root_resource, smbios_ep, PAGE_SIZE, &ep_mapping, &ep_start);
  if (status != ZX_OK) {
    return status;
  }

  auto ep = reinterpret_cast<const smbios::EntryPoint2_1*>(ep_start);
  if (!ep->IsValid()) {
    return ZX_ERR_IO_DATA_INTEGRITY;
  }

  // Map the struct table
  fzl::OwnedVmoMapper struct_table_mapping;
  uintptr_t struct_table_start;
  status = MapStructure(*root_resource, ep->struct_table_phys, ep->struct_table_length,
                        &struct_table_mapping, &struct_table_start);
  if (status != ZX_OK) {
    return status;
  }

  entry_point_mapping_ = std::move(ep_mapping);
  struct_table_mapping_ = std::move(struct_table_mapping);
  entry_point_ = ep;
  struct_table_start_ = struct_table_start;
  return ZX_OK;
}

}  // namespace

bool smbios_product_name_is_valid(const char* product_name) {
  if (product_name == nullptr || !strcmp(product_name, "<null>") || strlen(product_name) == 0) {
    return false;
  }
  // Check if the product name is all spaces (seen on some devices)
  const size_t product_name_len = strlen(product_name);
  bool nonspace_found = false;
  for (size_t i = 0; i < product_name_len; ++i) {
    if (product_name[i] != ' ') {
      nonspace_found = true;
      break;
    }
  }

  if (!nonspace_found) {
    return false;
  }

  return true;
}

zx_status_t SmbiosInfo::Load() {
  SmbiosState smbios;
  zx_status_t status = smbios.LoadFromFirmware();
  if (status != ZX_OK) {
    return status;
  }

  auto callback = [this](smbios::SpecVersion version, const smbios::Header* h,
                         const smbios::StringTable& st) -> zx_status_t {
    const char* name;
    switch (h->type) {
      case smbios::StructType::BiosInfo: {
        if (!version.IncludesVersion(2, 0)) {
          break;
        }
        auto entry = reinterpret_cast<const smbios::BiosInformationStruct2_0*>(h);
        zx_status_t status = st.GetString(entry->vendor_str_idx, &name);
        if (status == ZX_OK) {
          vendor_ = name;
        }
        break;
      }
      case smbios::StructType::SystemInfo: {
        if (!version.IncludesVersion(2, 0)) {
          break;
        }
        auto entry = reinterpret_cast<const smbios::SystemInformationStruct2_0*>(h);
        zx_status_t status = st.GetString(entry->product_name_str_idx, &name);
        if (status == ZX_OK && smbios_product_name_is_valid(name)) {
          board_name_ = name;
        }
        break;
      }
      case smbios::StructType::Baseboard: {
        auto entry = reinterpret_cast<const smbios::BaseboardInformationStruct*>(h);
        zx_status_t status = st.GetString(entry->product_name_str_idx, &name);
        if (status == ZX_OK && smbios_product_name_is_valid(name)) {
          board_name_ = name;
        }
        break;
      }
      default:
        break;
    }
    return ZX_OK;
  };
  return smbios.entry_point()->WalkStructs(smbios.struct_table_start(), callback);
}
