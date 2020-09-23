// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <align.h>
#include <fbl/alloc_checker.h>
#include <hypervisor/guest_physical_address_space.h>
#include <kernel/range_check.h>
#include <ktl/move.h>
#include <vm/fault.h>
#include <vm/vm_object_physical.h>

static constexpr uint kPfFlags = VMM_PF_FLAG_WRITE | VMM_PF_FLAG_SW_FAULT;

static constexpr uint kInterruptMmuFlags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;

static constexpr uint kGuestMmuFlags =
    ARCH_MMU_FLAG_CACHED | ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;

namespace hypervisor {

zx_status_t GuestPhysicalAddressSpace::Create(
#if ARCH_ARM64
    uint8_t vmid,
#endif
    ktl::unique_ptr<GuestPhysicalAddressSpace>* _gpas) {
  fbl::AllocChecker ac;
  auto gpas = ktl::make_unique<GuestPhysicalAddressSpace>(&ac);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  gpas->guest_aspace_ = VmAspace::Create(VmAspace::TYPE_GUEST_PHYS, "guest_paspace");
  if (!gpas->guest_aspace_) {
    return ZX_ERR_NO_MEMORY;
  }
#if ARCH_ARM64
  gpas->arch_aspace()->arch_set_asid(vmid);
#endif
  *_gpas = ktl::move(gpas);
  return ZX_OK;
}

GuestPhysicalAddressSpace::~GuestPhysicalAddressSpace() {
  // VmAspace maintains a circular reference with it's root VMAR. We need to
  // destroy the VmAspace in order to break that reference and allow the
  // VmAspace to be destructed.
  if (guest_aspace_) {
    guest_aspace_->Destroy();
  }
}

zx_status_t GuestPhysicalAddressSpace::MapInterruptController(zx_gpaddr_t guest_paddr,
                                                              zx_paddr_t host_paddr, size_t len) {
  fbl::RefPtr<VmObjectPhysical> vmo;
  zx_status_t status = VmObjectPhysical::Create(host_paddr, len, &vmo);
  if (status != ZX_OK) {
    return status;
  }

  status = vmo->SetMappingCachePolicy(ARCH_MMU_FLAG_UNCACHED_DEVICE);
  if (status != ZX_OK) {
    return status;
  }

  // The root VMAR will maintain a reference to the VmMapping internally so
  // we don't need to maintain a long-lived reference to the mapping here.
  fbl::RefPtr<VmMapping> mapping;
  status = RootVmar()->CreateVmMapping(guest_paddr, vmo->size(), /* align_pow2*/ 0,
                                       VMAR_FLAG_SPECIFIC, vmo, /* vmo_offset */ 0,
                                       kInterruptMmuFlags, "guest_interrupt_vmo", &mapping);
  if (status != ZX_OK) {
    return status;
  }

  // Write mapping to page table.
  status = mapping->MapRange(0, vmo->size(), true);
  if (status != ZX_OK) {
    mapping->Destroy();
    return status;
  }
  return ZX_OK;
}

zx_status_t GuestPhysicalAddressSpace::UnmapRange(zx_gpaddr_t guest_paddr, size_t len) {
  return RootVmar()->UnmapAllowPartial(guest_paddr, len);
}

static fbl::RefPtr<VmMapping> FindMapping(fbl::RefPtr<VmAddressRegion> region,
                                          zx_gpaddr_t guest_paddr) {
  for (fbl::RefPtr<VmAddressRegionOrMapping> next; (next = region->FindRegion(guest_paddr));
       region = next->as_vm_address_region()) {
    if (next->is_mapping()) {
      return next->as_vm_mapping();
    }
  }
  return nullptr;
}

zx_status_t GuestPhysicalAddressSpace::GetPage(zx_gpaddr_t guest_paddr, zx_paddr_t* host_paddr) {
  fbl::RefPtr<VmMapping> mapping = FindMapping(RootVmar(), guest_paddr);
  if (!mapping) {
    return ZX_ERR_NOT_FOUND;
  }

  // Lookup the physical address of this page in the VMO.
  zx_gpaddr_t offset = guest_paddr - mapping->base();
  return mapping->vmo()->GetPage(offset, kPfFlags, nullptr, nullptr, nullptr, host_paddr);
}

zx_status_t GuestPhysicalAddressSpace::PageFault(zx_gpaddr_t guest_paddr) {
  fbl::RefPtr<VmMapping> mapping = FindMapping(RootVmar(), guest_paddr);
  if (!mapping) {
    return ZX_ERR_NOT_FOUND;
  }

  // In order to avoid re-faulting if the guest changes how it accesses guest
  // physical memory, and to avoid the need for invalidation of the guest
  // physical address space on x86 (through the use of INVEPT), we fault the
  // page with the maximum allowable permissions of the mapping.
  uint pf_flags = VMM_PF_FLAG_GUEST | VMM_PF_FLAG_HW_FAULT;
  if (mapping->arch_mmu_flags() & ARCH_MMU_FLAG_PERM_WRITE) {
    pf_flags |= VMM_PF_FLAG_WRITE;
  }
  if (mapping->arch_mmu_flags() & ARCH_MMU_FLAG_PERM_EXECUTE) {
    pf_flags |= VMM_PF_FLAG_INSTRUCTION;
  }
  Guard<Mutex> guard{guest_aspace_->lock()};
  return mapping->PageFault(guest_paddr, pf_flags, nullptr);
}

zx_status_t GuestPhysicalAddressSpace::CreateGuestPtr(zx_gpaddr_t guest_paddr, size_t len,
                                                      const char* name, GuestPtr* guest_ptr) {
  const zx_gpaddr_t begin = ROUNDDOWN(guest_paddr, PAGE_SIZE);
  const zx_gpaddr_t end = ROUNDUP(guest_paddr + len, PAGE_SIZE);
  const zx_gpaddr_t mapping_len = end - begin;
  if (begin > end || !InRange(begin, mapping_len, size())) {
    return ZX_ERR_INVALID_ARGS;
  }
  fbl::RefPtr<VmAddressRegionOrMapping> region = RootVmar()->FindRegion(begin);
  if (!region) {
    return ZX_ERR_NOT_FOUND;
  }
  fbl::RefPtr<VmMapping> guest_mapping = region->as_vm_mapping();
  if (!guest_mapping) {
    return ZX_ERR_WRONG_TYPE;
  }
  const uint64_t intra_mapping_offset = begin - guest_mapping->base();
  if (!InRange(intra_mapping_offset, mapping_len, guest_mapping->size())) {
    // The address range is not contained within a single mapping.
    return ZX_ERR_OUT_OF_RANGE;
  }

  fbl::RefPtr<VmMapping> host_mapping;
  zx_status_t status = VmAspace::kernel_aspace()->RootVmar()->CreateVmMapping(
      /* mapping_offset */ 0, mapping_len,
      /* align_pow2 */ false,
      /* vmar_flags */ 0, guest_mapping->vmo(),
      guest_mapping->object_offset() + intra_mapping_offset, kGuestMmuFlags, name, &host_mapping);
  if (status != ZX_OK) {
    return status;
  }

  *guest_ptr = GuestPtr(ktl::move(host_mapping), guest_paddr - begin);
  return ZX_OK;
}

}  // namespace hypervisor
