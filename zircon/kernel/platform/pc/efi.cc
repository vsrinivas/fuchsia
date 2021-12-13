// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/interrupt.h>
#include <fbl/ref_ptr.h>
#include <ktl/optional.h>
#include <platform/pc/efi.h>
#include <vm/vm_aspace.h>

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
    vmm_set_active_aspace_locked(efi_aspace.get());
  } else {
    vmm_set_active_aspace(efi_aspace.get());
  }
}

}  // namespace

zx_status_t InitEfiServices(uint64_t efi_system_table) {
  ZX_ASSERT(!gEfiSystemTable);
  gEfiSystemTable = efi_system_table;

  // Create a new address space.
  efi_aspace = VmAspace::Create(VmAspace::Type::LowKernel, "uefi");
  if (!efi_aspace) {
    return ZX_ERR_NO_RESOURCES;
  }

  // Map in EFI services.
  //
  // We map the first 16 GiB of address space 1:1 from virt/phys.
  //
  // TODO: Be more precise about this. This gets the job done on the platforms
  // we're working on right now, but is probably not entirely correct.
  auto* desired_location = static_cast<void*>(0);  // NOLINT
  zx_status_t result = efi_aspace->AllocPhysical(
      "1:1", 16 * 1024 * 1024 * 1024UL, &desired_location, PAGE_SIZE_SHIFT, 0,
      VmAspace::VMM_FLAG_VALLOC_SPECIFIC,
      ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE | ARCH_MMU_FLAG_PERM_EXECUTE);
  if (result != ZX_OK) {
    efi_aspace.reset();
    return result;
  }

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
