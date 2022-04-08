// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/process.h>

#include <acpica/acpi.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/mutex.h>

#include "zircon/system/ulib/acpica/osfuchsia.h"

namespace {
class AcpiOsMappingNode : public fbl::SinglyLinkedListable<std::unique_ptr<AcpiOsMappingNode>> {
 public:
  using HashTable = fbl::HashTable<uintptr_t, std::unique_ptr<AcpiOsMappingNode>>;

  // @param vaddr Virtual address returned to ACPI, used as key to the hashtable.
  // @param vaddr_actual Actual virtual address of the mapping. May be different than
  //                     vaddr if it is unaligned.
  // @param length Length of the mapping
  // @param vmo_handle Handle to the mapped VMO
  AcpiOsMappingNode(uintptr_t vaddr, uintptr_t vaddr_actual, size_t length, zx_handle_t vmo_handle);
  ~AcpiOsMappingNode();

  // Trait implementation for fbl::HashTable
  uintptr_t GetKey() const { return vaddr_; }
  static size_t GetHash(uintptr_t key) { return key; }

 private:
  uintptr_t vaddr_;
  uintptr_t vaddr_actual_;
  size_t length_;
  zx_handle_t vmo_handle_;
};

AcpiOsMappingNode::AcpiOsMappingNode(uintptr_t vaddr, uintptr_t vaddr_actual, size_t length,
                                     zx_handle_t vmo_handle)
    : vaddr_(vaddr), vaddr_actual_(vaddr_actual), length_(length), vmo_handle_(vmo_handle) {}

AcpiOsMappingNode::~AcpiOsMappingNode() {
  zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)vaddr_actual_, length_);
  zx_handle_close(vmo_handle_);
}

fbl::Mutex os_mapping_lock;
AcpiOsMappingNode::HashTable os_mapping_tbl;
}  // namespace

static zx_status_t mmap_physical(zx_paddr_t phys, size_t size, uint32_t cache_policy,
                                 zx_handle_t* out_vmo, zx_vaddr_t* out_vaddr) {
  zx_handle_t vmo;
  zx_vaddr_t vaddr;
  zx_status_t st = zx_vmo_create_physical(root_resource_handle, phys, size, &vmo);
  if (st != ZX_OK) {
    return st;
  }
  st = zx_vmo_set_cache_policy(vmo, cache_policy);
  if (st != ZX_OK) {
    zx_handle_close(vmo);
    return st;
  }
  st = zx_vmar_map(zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_MAP_RANGE, 0,
                   vmo, 0, size, &vaddr);
  if (st != ZX_OK) {
    zx_handle_close(vmo);
    return st;
  } else {
    *out_vmo = vmo;
    *out_vaddr = vaddr;
    return ZX_OK;
  }
}

/**
 * @brief Map physical memory into the caller's address space.
 *
 * @param PhysicalAddress A full physical address of the memory to be mapped
 *        into the caller's address space
 * @param Length The amount of memory to mapped starting at the given physical
 *        address
 *
 * @return Logical pointer to the mapped memory. A NULL pointer indicated failures.
 */
void* AcpiOsMapMemory(ACPI_PHYSICAL_ADDRESS PhysicalAddress, ACPI_SIZE Length) {
  fbl::AutoLock lock(&os_mapping_lock);

  // Caution: PhysicalAddress might not be page-aligned, Length might not
  // be a page multiple.

  const size_t kPageSize = zx_system_get_page_size();
  ACPI_PHYSICAL_ADDRESS aligned_address = PhysicalAddress & ~(kPageSize - 1);
  ACPI_PHYSICAL_ADDRESS end = (PhysicalAddress + Length + kPageSize - 1) & ~(kPageSize - 1);

  uintptr_t vaddr;
  size_t length = end - aligned_address;
  zx_handle_t vmo;
  zx_status_t status =
      mmap_physical(aligned_address, end - aligned_address, ZX_CACHE_POLICY_CACHED, &vmo, &vaddr);
  if (status != ZX_OK) {
    return NULL;
  }

  void* out_addr = (void*)(vaddr + (PhysicalAddress - aligned_address));
  std::unique_ptr<AcpiOsMappingNode> mn(
      new AcpiOsMappingNode(reinterpret_cast<uintptr_t>(out_addr), vaddr, length, vmo));
  os_mapping_tbl.insert(std::move(mn));

  return out_addr;
}

/**
 * @brief Remove a physical to logical memory mapping.
 *
 * @param LogicalAddress The logical address that was returned from a previous
 *        call to AcpiOsMapMemory.
 * @param Length The amount of memory that was mapped. This value must be
 *        identical to the value used in the call to AcpiOsMapMemory.
 */
void AcpiOsUnmapMemory(void* LogicalAddress, ACPI_SIZE Length) {
  fbl::AutoLock lock(&os_mapping_lock);
  std::unique_ptr<AcpiOsMappingNode> mn = os_mapping_tbl.erase((uintptr_t)LogicalAddress);
  if (mn == NULL) {
    printf("AcpiOsUnmapMemory nonexisting mapping %p\n", LogicalAddress);
  }
}
