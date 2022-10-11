// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/fit/defer.h>

#include <arch/interrupt.h>
#include <efi/boot-services.h>
#include <efi/types.h>
#include <fbl/ref_ptr.h>
#include <ktl/optional.h>
#include <lk/init.h>
#include <phys/handoff.h>
#include <platform/efi.h>
#include <vm/bootreserve.h>
#include <vm/vm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object_physical.h>

#include "efi-private.h"

#include <ktl/enforce.h>

namespace {

// EFI system table physical address.
ktl::optional<uint64_t> gEfiSystemTable;

// Address space with EFI services mapped in 1:1.
fbl::RefPtr<VmAspace> efi_aspace;

// Switch into the given address space in a panic-handler friendly manner.
//
// In some contexts (such as panicking) the thread lock may already be
// held, in which case we avoid grabbing the lock again.
void PanicFriendlySwitchAspace(VmAspace* aspace) {
  InterruptDisableGuard interrupt_guard;
  if (thread_lock.IsHeld()) {
    vmm_set_active_aspace_locked(aspace);
  } else {
    vmm_set_active_aspace(aspace);
  }
}

void EfiInitHook(uint level) {
  // Attempt to initialize EFI.
  if (gPhysHandoff->efi_system_table) {
    zx_status_t status = InitEfiServices(gPhysHandoff->efi_system_table.value());
    if (status != ZX_OK) {
      dprintf(INFO, "Unable to initialize EFI services: %d\n", status);
      return;
    } else {
      printf("Init EFI OK\n");
    }
  } else {
    dprintf(INFO, "No EFI available on system.\n");
    return;
  }
}

// Init EFI before INIT_LEVEL_PLATFORM in case the platform code wants to use the EFI crashlog.
LK_INIT_HOOK(efi_init, EfiInitHook, LK_INIT_LEVEL_PLATFORM - 1)

// Helper function that maps the region with size |size| at |base| into the given aspace.
zx_status_t MapUnalignedRegion(VmAspace* aspace, paddr_t base, size_t size, const char* name,
                               uint arch_mmu_flags) {
  auto vmar = aspace->RootVmar();

  bool is_reserved = !boot_reserve_foreach([base, size](reserve_range_t range) {
    if (base >= range.pa && (base + size) <= (range.pa + range.len)) {
      // Found the range, return false to early exit.
      return false;
    }
    return true;
  });
  if (!is_reserved) {
    printf("ERROR: Attempted to map EFI region [0x%lx, 0x%zx), which is not a reserved region.\n",
           base, base + size);
    return ZX_ERR_INVALID_ARGS;
  }

  fbl::RefPtr<VmObjectPhysical> vmo;
  paddr_t aligned_base = ROUNDDOWN(base, PAGE_SIZE);
  size_t aligned_size = PAGE_ALIGN(size + (base - aligned_base));
  zx_status_t status = VmObjectPhysical::Create(aligned_base, aligned_size, &vmo);
  if (status != ZX_OK) {
    return status;
  }

  uint32_t vmar_flags = VMAR_FLAG_SPECIFIC_OVERWRITE;
  if (arch_mmu_flags & ARCH_MMU_FLAG_PERM_READ) {
    vmar_flags |= VMAR_FLAG_CAN_MAP_READ;
  }
  if (arch_mmu_flags & ARCH_MMU_FLAG_PERM_WRITE) {
    vmar_flags |= VMAR_FLAG_CAN_MAP_WRITE;
  }
  if (arch_mmu_flags & ARCH_MMU_FLAG_PERM_EXECUTE) {
    vmar_flags |= VMAR_FLAG_CAN_MAP_EXECUTE;
  }

  fbl::RefPtr<VmMapping> mapping;
  status = vmar->CreateVmMapping(aligned_base, aligned_size, ZX_PAGE_SHIFT, vmar_flags, vmo, 0,
                                 arch_mmu_flags, name, &mapping);
  if (status != ZX_OK) {
    return status;
  }

  status = mapping->MapRange(0, aligned_size, true);
  if (status != ZX_OK) {
    return status;
  }

  return ZX_OK;
}

}  // namespace

// Loops over a ktl::span<ktl::byte> that may or may not be a valid
// efi_memory_attributes_table.
// Will return early if |callback| does not return ZX_OK.
// Returns ZX_ERR_INVALID_ARGS if |table| is invalid.
zx_status_t ForEachMemoryAttributeEntrySafe(
    ktl::span<const ktl::byte> table,
    fit::inline_function<zx_status_t(const efi_memory_descriptor*)> callback) {
  if (table.size() < sizeof(efi_memory_attributes_table_header)) {
    return ZX_ERR_INVALID_ARGS;
  }
  auto header_bytes = table.subspan(0, sizeof(efi_memory_attributes_table_header));
  const efi_memory_attributes_table_header* header =
      reinterpret_cast<const efi_memory_attributes_table_header*>(header_bytes.data());

  auto entries = table.subspan(sizeof(efi_memory_attributes_table_header));

  if (header->descriptor_size < sizeof(efi_memory_descriptor)) {
    dprintf(CRITICAL,
            "EFI memory attributes header reports a descriptor size of 0x%x, which is smaller than "
            "ours (0x%zx)\n",
            header->descriptor_size, sizeof(efi_memory_descriptor));
    return ZX_ERR_INVALID_ARGS;
  }

  for (size_t i = 0; i < header->number_of_entries; i++) {
    if (entries.size() < sizeof(efi_memory_descriptor)) {
      return ZX_ERR_INVALID_ARGS;
    }
    ktl::span<const ktl::byte> entry = entries.subspan(0, sizeof(efi_memory_descriptor));

    const efi_memory_descriptor* desc =
        reinterpret_cast<const efi_memory_descriptor*>(entry.data());

    zx_status_t status = callback(desc);
    if (status != ZX_OK) {
      return status;
    }

    if (header->descriptor_size > entries.size()) {
      return ZX_ERR_INVALID_ARGS;
    }
    entries = entries.subspan(header->descriptor_size);
  }

  return ZX_OK;
}

zx_status_t InitEfiServices(uint64_t efi_system_table) {
  ZX_ASSERT(!gEfiSystemTable);
  gEfiSystemTable = efi_system_table;

  // Create a new address space.
  efi_aspace = VmAspace::Create(VmAspace::Type::LowKernel, "uefi");
  if (!efi_aspace) {
    return ZX_ERR_NO_RESOURCES;
  }
  auto error_cleanup = fit::defer([]() { efi_aspace.reset(); });

  // gPhysHandoff currently points into physical pages that are part of the ZBI VMO.
  // This is safe for now, because we call the EfiInitHook at LK_INIT_LEVEL_PLATFORM, which is
  // before userboot runs.
  // There are plans to change this in the future, at which point we may need to revisit this.
  if (gPhysHandoff->efi_memory_attributes.get().size() == 0) {
    dprintf(CRITICAL, "EFI did not provide memory table, cannot map runtime services.\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Map in the system table.
  const efi_memory_attributes_table_header* efi_memory_table =
      reinterpret_cast<const efi_memory_attributes_table_header*>(
          gPhysHandoff->efi_memory_attributes.get().begin());

  if (efi_memory_table == nullptr) {
    dprintf(CRITICAL, "EFI did not provide memory table, cannot map runtime services.\n");
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t status = ForEachMemoryAttributeEntrySafe(
      gPhysHandoff->efi_memory_attributes.get(), [](const efi_memory_descriptor* desc) {
        if (!(desc->Attribute & EFI_MEMORY_RUNTIME)) {
          return ZX_OK;
        }
        if (desc->Type != EfiRuntimeServicesCode && desc->Type != EfiRuntimeServicesData) {
          return ZX_OK;
        }

        uint arch_mmu_flags = ARCH_MMU_FLAG_PERM_RWX_MASK;
        // UEFI v2.9, section 4.6, "EFI_MEMORY_ATTRIBUTES_TABLE" says that only RUNTIME, RO and XP
        // are allowed to be set.
        if (desc->Attribute & EFI_MEMORY_RO) {
          arch_mmu_flags &= ~ARCH_MMU_FLAG_PERM_WRITE;
        }
        if (desc->Attribute & EFI_MEMORY_XP) {
          arch_mmu_flags &= ~ARCH_MMU_FLAG_PERM_EXECUTE;
        }
        zx_status_t result =
            MapUnalignedRegion(efi_aspace.get(), desc->PhysicalStart,
                               desc->NumberOfPages * PAGE_SIZE, "efi_runtime", arch_mmu_flags);
        if (result != ZX_OK) {
          dprintf(CRITICAL, "Failed to map EFI region base=0x%lx size=0x%lx: %d\n",
                  desc->PhysicalStart, desc->NumberOfPages * PAGE_SIZE, result);
          return result;
        }

        return ZX_OK;
      });

  if (status != ZX_OK) {
    return status;
  }

  error_cleanup.cancel();
  return ZX_OK;
}

EfiServicesActivation TryActivateEfiServices() {
  // Ensure we have EFI services available and it has been initialised.
  if (efi_aspace == nullptr) {
    return EfiServicesActivation::Null();
  }
  ZX_DEBUG_ASSERT(gEfiSystemTable);

  // Switch into the address space where EFI services have been mapped.
  VmAspace* old_aspace = Thread::Current::Get()->aspace();
  PanicFriendlySwitchAspace(efi_aspace.get());

  // Return the services.
  efi_system_table* sys = reinterpret_cast<efi_system_table*>(*gEfiSystemTable);
  return EfiServicesActivation(old_aspace, sys->RuntimeServices);
}

void EfiServicesActivation::reset() {
  if (previous_aspace_ == nullptr) {
    return;
  }

  // Restore the previous address space.
  PanicFriendlySwitchAspace(previous_aspace_);
  previous_aspace_ = nullptr;
}
