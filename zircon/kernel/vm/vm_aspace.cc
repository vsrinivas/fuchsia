// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "vm/vm_aspace.h"

#include <align.h>
#include <assert.h>
#include <inttypes.h>
#include <lib/boot-options/boot-options.h>
#include <lib/counters.h>
#include <lib/crypto/global_prng.h>
#include <lib/crypto/prng.h>
#include <lib/ktrace.h>
#include <lib/lazy_init/lazy_init.h>
#include <lib/userabi/vdso.h>
#include <lib/zircon-internal/macros.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <arch/kernel_aspace.h>
#include <fbl/alloc_checker.h>
#include <fbl/intrusive_double_list.h>
#include <kernel/mutex.h>
#include <kernel/thread.h>
#include <kernel/thread_lock.h>
#include <ktl/algorithm.h>
#include <object/process_dispatcher.h>
#include <vm/fault.h>
#include <vm/vm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_object.h>
#include <vm/vm_object_paged.h>
#include <vm/vm_object_physical.h>

#include "vm_priv.h"

#include <ktl/enforce.h>

#define LOCAL_TRACE VM_GLOBAL_TRACE(0)

#define GUEST_PHYSICAL_ASPACE_BASE 0UL
#define GUEST_PHYSICAL_ASPACE_SIZE (1UL << MMU_GUEST_SIZE_SHIFT)

// pointer to a singleton kernel address space
VmAspace* VmAspace::kernel_aspace_ = nullptr;

// singleton list of all aspaces in the system.
fbl::DoublyLinkedList<VmAspace*> VmAspace::aspaces_list_ = {};

namespace {

KCOUNTER(vm_aspace_marked_latency_sensitive, "vm.aspace.latency_sensitive.marked")
KCOUNTER(vm_aspace_latency_sensitive_destroyed, "vm.aspace.latency_sensitive.destroyed")
KCOUNTER(vm_aspace_accessed_harvests_performed, "vm.aspace.accessed_harvest.performed")
KCOUNTER(vm_aspace_accessed_harvests_skipped, "vm.aspace.accessed_harvest.skipped")

// the singleton kernel address space
lazy_init::LazyInit<VmAspace, lazy_init::CheckType::None, lazy_init::Destructor::Disabled>
    g_kernel_aspace;
lazy_init::LazyInit<VmAddressRegion, lazy_init::CheckType::None, lazy_init::Destructor::Disabled>
    g_kernel_root_vmar;

// simple test routines
// Returns true if the base + size is valid for the given |type|.
inline bool is_valid_for_type(vaddr_t base, size_t size, VmAspace::Type type) {
  if (base + size < base) {
    return false;
  }

  vaddr_t min = 0;
  vaddr_t max = 0;
  switch (type) {
    case VmAspace::Type::User:
      min = USER_ASPACE_BASE;
      max = USER_ASPACE_BASE + USER_ASPACE_SIZE;
      break;
    case VmAspace::Type::Kernel:
      min = KERNEL_ASPACE_BASE;
      max = KERNEL_ASPACE_BASE + KERNEL_ASPACE_SIZE;
      break;
    case VmAspace::Type::LowKernel:
      min = 0;
      max = USER_ASPACE_BASE + USER_ASPACE_SIZE;
      break;
    case VmAspace::Type::GuestPhysical:
      min = GUEST_PHYSICAL_ASPACE_BASE;
      max = GUEST_PHYSICAL_ASPACE_BASE + GUEST_PHYSICAL_ASPACE_SIZE;
      break;
    default:
      panic("Invalid aspace type");
  }
  return base >= min && base + size <= max;
}

uint arch_aspace_flags_from_type(VmAspace::Type type) {
  bool is_high_kernel = type == VmAspace::Type::Kernel;
  bool is_guest = type == VmAspace::Type::GuestPhysical;
  return (is_high_kernel ? ARCH_ASPACE_FLAG_KERNEL : 0u) | (is_guest ? ARCH_ASPACE_FLAG_GUEST : 0u);
}

}  // namespace

// Called once at boot to initialize the singleton kernel address
// space. Thread safety analysis is disabled since we don't need to
// lock yet.
void VmAspace::KernelAspaceInitPreHeap() TA_NO_THREAD_SAFETY_ANALYSIS {
  g_kernel_aspace.Initialize(KERNEL_ASPACE_BASE, KERNEL_ASPACE_SIZE, VmAspace::Type::Kernel,
                             CreateAslrConfig(VmAspace::Type::Kernel), "kernel");

#if LK_DEBUGLEVEL > 1
  g_kernel_aspace->Adopt();
#endif

  g_kernel_root_vmar.Initialize(g_kernel_aspace.Get());
  g_kernel_aspace->root_vmar_ = fbl::AdoptRef(&g_kernel_root_vmar.Get());

  zx_status_t status = g_kernel_aspace->Init();
  ASSERT(status == ZX_OK);

  // save a pointer to the singleton kernel address space
  VmAspace::kernel_aspace_ = &g_kernel_aspace.Get();
  aspaces_list_.push_front(kernel_aspace_);
}

VmAspace::VmAspace(vaddr_t base, size_t size, Type type, AslrConfig aslr_config, const char* name)
    : base_(base),
      size_(size),
      type_(type),
      root_vmar_(nullptr),
      aslr_prng_(nullptr, 0),
      aslr_config_(aslr_config),
      arch_aspace_(base, size, arch_aspace_flags_from_type(type)) {
  DEBUG_ASSERT(size != 0);
  DEBUG_ASSERT(base + size - 1 >= base);

  Rename(name);

  LTRACEF("%p '%s'\n", this, name_);
}

zx_status_t VmAspace::Init() {
  canary_.Assert();

  LTRACEF("%p '%s'\n", this, name_);

  // initialize the architecturally specific part
  zx_status_t status = arch_aspace_.Init();
  if (status != ZX_OK) {
    return status;
  }

  InitializeAslr();

  Guard<CriticalMutex> guard{&lock_};

  if (likely(!root_vmar_)) {
    return VmAddressRegion::CreateRootLocked(*this, VMAR_FLAG_CAN_MAP_SPECIFIC, &root_vmar_);
  }
  return ZX_OK;
}

fbl::RefPtr<VmAspace> VmAspace::Create(vaddr_t base, size_t size, Type type, const char* name) {
  LTRACEF("type %u, name '%s'\n", static_cast<uint>(type), name);

  if (!is_valid_for_type(base, size, type)) {
    return nullptr;
  }

  fbl::AllocChecker ac;
  auto aspace = fbl::AdoptRef(new (&ac) VmAspace(base, size, type, CreateAslrConfig(type), name));
  if (!ac.check()) {
    return nullptr;
  }

  // initialize the arch specific component to our address space
  zx_status_t status = aspace->Init();
  if (status != ZX_OK) {
    status = aspace->Destroy();
    DEBUG_ASSERT(status == ZX_OK);
    return nullptr;
  }

  // add it to the global list
  {
    Guard<Mutex> guard{AspaceListLock::Get()};
    aspaces_list_.push_back(aspace.get());
  }

  // return a ref pointer to the aspace
  return aspace;
}

fbl::RefPtr<VmAspace> VmAspace::Create(Type type, const char* name) {
  vaddr_t base;
  size_t size;
  switch (type) {
    case Type::User:
      base = USER_ASPACE_BASE;
      size = USER_ASPACE_SIZE;
      break;
    case Type::Kernel:
      base = KERNEL_ASPACE_BASE;
      size = KERNEL_ASPACE_SIZE;
      break;
    case Type::LowKernel:
      base = 0;
      size = USER_ASPACE_BASE + USER_ASPACE_SIZE;
      break;
    case Type::GuestPhysical:
      base = GUEST_PHYSICAL_ASPACE_BASE;
      size = GUEST_PHYSICAL_ASPACE_SIZE;
      break;
    default:
      panic("Invalid aspace type");
  }

  return Create(base, size, type, name);
}

void VmAspace::Rename(const char* name) {
  canary_.Assert();

  Guard<CriticalMutex> guard{&lock_};
  strlcpy(name_, name ? name : "unnamed", sizeof(name_));
}

VmAspace::~VmAspace() {
  canary_.Assert();
  LTRACEF("%p '%s'\n", this, name_);

  // we have to have already been destroyed before freeing
  DEBUG_ASSERT(aspace_destroyed_);

  // pop it out of the global aspace list
  {
    Guard<Mutex> guard{AspaceListLock::Get()};
    if (this->InContainer()) {
      aspaces_list_.erase(*this);
    }
  }

  // destroy the arch portion of the aspace
  // TODO(teisenbe): Move this to Destroy().  Currently can't move since
  // ProcessDispatcher calls Destroy() from the context of a thread in the
  // aspace and HarvestAllUserPageTables assumes the arch_aspace is valid if
  // the aspace is in the global list.
  zx_status_t status = arch_aspace_.Destroy();
  DEBUG_ASSERT(status == ZX_OK);

  // Update any counters.
  if (IsLatencySensitive()) {
    vm_aspace_latency_sensitive_destroyed.Add(1);
  }
}

fbl::RefPtr<VmAddressRegion> VmAspace::RootVmar() {
  Guard<CriticalMutex> guard{&lock_};
  return RootVmarLocked();
}

fbl::RefPtr<VmAddressRegion> VmAspace::RootVmarLocked() { return root_vmar_; }

zx_status_t VmAspace::Destroy() {
  canary_.Assert();
  LTRACEF("%p '%s'\n", this, name_);

  Guard<CriticalMutex> guard{&lock_};

  // Don't let a vDSO mapping prevent destroying a VMAR
  // when the whole process is being destroyed.
  vdso_code_mapping_.reset();

  // tear down and free all of the regions in our address space
  if (root_vmar_) {
    AssertHeld(root_vmar_->lock_ref());
    zx_status_t status = root_vmar_->DestroyLocked();
    if (status != ZX_OK && status != ZX_ERR_BAD_STATE) {
      return status;
    }
  }
  aspace_destroyed_ = true;

  root_vmar_.reset();

  // Now that we've removed all mappings we can put the arch aspace into a sort of read-only mode.
  //
  // TODO(fxbug.dev/79118): Once fxbug.dev/79118 is resolved, this call (and the DisableUpdates
  // feature) can be removed.
  arch_aspace_.DisableUpdates();

  return ZX_OK;
}

bool VmAspace::is_destroyed() const {
  Guard<CriticalMutex> guard{&lock_};
  return aspace_destroyed_;
}

zx_status_t VmAspace::MapObjectInternal(fbl::RefPtr<VmObject> vmo, const char* name,
                                        uint64_t offset, size_t size, void** ptr,
                                        uint8_t align_pow2, uint vmm_flags, uint arch_mmu_flags) {
  canary_.Assert();
  LTRACEF("aspace %p name '%s' vmo %p, offset %#" PRIx64
          " size %#zx "
          "ptr %p align %hhu vmm_flags %#x arch_mmu_flags %#x\n",
          this, name, vmo.get(), offset, size, ptr ? *ptr : 0, align_pow2, vmm_flags,
          arch_mmu_flags);

  DEBUG_ASSERT(!is_user());

  size = ROUNDUP(size, PAGE_SIZE);
  if (size == 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (!vmo) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (!IS_PAGE_ALIGNED(offset)) {
    return ZX_ERR_INVALID_ARGS;
  }

  vaddr_t vmar_offset = 0;
  // if they're asking for a specific spot or starting address, copy the address
  if (vmm_flags & VMM_FLAG_VALLOC_SPECIFIC) {
    // can't ask for a specific spot and then not provide one
    if (!ptr) {
      return ZX_ERR_INVALID_ARGS;
    }
    vmar_offset = reinterpret_cast<vaddr_t>(*ptr);

    // check that it's page aligned
    if (!IS_PAGE_ALIGNED(vmar_offset) || vmar_offset < base_) {
      return ZX_ERR_INVALID_ARGS;
    }

    vmar_offset -= base_;
  }

  uint32_t vmar_flags = 0;
  if (vmm_flags & VMM_FLAG_VALLOC_SPECIFIC) {
    vmar_flags |= VMAR_FLAG_SPECIFIC;
  }

  // Create the mappings with all of the CAN_* RWX flags, so that
  // Protect() can transition them arbitrarily.  This is not desirable for the
  // long-term.
  vmar_flags |= VMAR_CAN_RWX_FLAGS;

  // TODO: Enforce all callers to be passing VMM_FLAG_COMMIT.
  zx_status_t status = vmo->CommitRangePinned(offset, size, true);
  if (status != ZX_OK) {
    return status;
  }

  // allocate a region and put it in the aspace list
  fbl::RefPtr<VmMapping> r(nullptr);
  status = RootVmar()->CreateVmMapping(vmar_offset, size, align_pow2, vmar_flags, vmo, offset,
                                       arch_mmu_flags, name, &r);
  if (status != ZX_OK) {
    return status;
  }

  // if we're committing it, map the region now
  // TODO: Enforce all callers to be passing VMM_FLAG_COMMIT.
  if (vmm_flags & VMM_FLAG_COMMIT) {
    status = r->MapRange(0, size, true);
    if (status != ZX_OK) {
      return status;
    }
  }

  // return the vaddr if requested
  if (ptr) {
    *ptr = (void*)r->base();
  }

  return ZX_OK;
}

zx_status_t VmAspace::AllocPhysical(const char* name, size_t size, void** ptr, uint8_t align_pow2,
                                    paddr_t paddr, uint vmm_flags, uint arch_mmu_flags) {
  canary_.Assert();
  LTRACEF("aspace %p name '%s' size %#zx ptr %p paddr %#" PRIxPTR
          " vmm_flags 0x%x arch_mmu_flags 0x%x\n",
          this, name, size, ptr ? *ptr : 0, paddr, vmm_flags, arch_mmu_flags);

  DEBUG_ASSERT(IS_PAGE_ALIGNED(paddr));

  if (size == 0) {
    return ZX_OK;
  }
  if (!IS_PAGE_ALIGNED(paddr)) {
    return ZX_ERR_INVALID_ARGS;
  }

  size = ROUNDUP_PAGE_SIZE(size);

  // create a vm object to back it
  fbl::RefPtr<VmObjectPhysical> vmo;
  zx_status_t status = VmObjectPhysical::Create(paddr, size, &vmo);
  if (status != ZX_OK) {
    return status;
  }
  vmo->set_name(name, strlen(name));

  // force it to be mapped up front
  // TODO: add new flag to precisely mean pre-map
  vmm_flags |= VMM_FLAG_COMMIT;

  // Apply the cache policy
  if (vmo->SetMappingCachePolicy(arch_mmu_flags & ARCH_MMU_FLAG_CACHE_MASK) != ZX_OK) {
    return ZX_ERR_INVALID_ARGS;
  }

  arch_mmu_flags &= ~ARCH_MMU_FLAG_CACHE_MASK;
  return MapObjectInternal(ktl::move(vmo), name, 0, size, ptr, align_pow2, vmm_flags,
                           arch_mmu_flags);
}

zx_status_t VmAspace::AllocContiguous(const char* name, size_t size, void** ptr, uint8_t align_pow2,
                                      uint vmm_flags, uint arch_mmu_flags) {
  canary_.Assert();
  LTRACEF("aspace %p name '%s' size 0x%zx ptr %p align %hhu vmm_flags 0x%x arch_mmu_flags 0x%x\n",
          this, name, size, ptr ? *ptr : 0, align_pow2, vmm_flags, arch_mmu_flags);

  size = ROUNDUP(size, PAGE_SIZE);
  if (size == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  // test for invalid flags
  if (!(vmm_flags & VMM_FLAG_COMMIT)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // create a vm object to back it
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::CreateContiguous(PMM_ALLOC_FLAG_ANY, size, align_pow2, &vmo);
  if (status != ZX_OK) {
    return status;
  }
  vmo->set_name(name, strlen(name));

  return MapObjectInternal(ktl::move(vmo), name, 0, size, ptr, align_pow2, vmm_flags,
                           arch_mmu_flags);
}

zx_status_t VmAspace::Alloc(const char* name, size_t size, void** ptr, uint8_t align_pow2,
                            uint vmm_flags, uint arch_mmu_flags) {
  canary_.Assert();
  LTRACEF("aspace %p name '%s' size 0x%zx ptr %p align %hhu vmm_flags 0x%x arch_mmu_flags 0x%x\n",
          this, name, size, ptr ? *ptr : 0, align_pow2, vmm_flags, arch_mmu_flags);

  size = ROUNDUP(size, PAGE_SIZE);
  if (size == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  // allocate a vm object to back it
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0u, size, &vmo);
  if (status != ZX_OK) {
    return status;
  }
  vmo->set_name(name, strlen(name));

  // map it, creating a new region
  return MapObjectInternal(ktl::move(vmo), name, 0, size, ptr, align_pow2, vmm_flags,
                           arch_mmu_flags);
}

zx_status_t VmAspace::FreeRegion(vaddr_t va) {
  DEBUG_ASSERT(!is_user());

  fbl::RefPtr<VmAddressRegionOrMapping> root_vmar = RootVmar();
  if (!root_vmar) {
    return ZX_ERR_NOT_FOUND;
  }
  fbl::RefPtr<VmAddressRegionOrMapping> r = RootVmar()->FindRegion(va);
  if (!r) {
    return ZX_ERR_NOT_FOUND;
  }

  fbl::RefPtr<VmMapping> mapping = r->as_vm_mapping();
  if (!mapping) {
    return ZX_ERR_BAD_STATE;
  }
  // Cache the VMO information for this mapping so that we can unpin. We must destroy the mapping
  // first though, otherwise we would be unpinning a live mapping.
  fbl::RefPtr<VmObject> vmo = mapping->vmo();
  uint64_t vmo_offset = 0;
  uint64_t unpin_size = 0;
  {
    Guard<CriticalMutex> guard{mapping->lock()};
    vmo_offset = mapping->object_offset_locked();
    unpin_size = mapping->size();
  }
  zx_status_t status = mapping->Destroy();
  vmo->Unpin(vmo_offset, unpin_size);
  return status;
}

fbl::RefPtr<VmAddressRegionOrMapping> VmAspace::FindRegion(vaddr_t va) {
  fbl::RefPtr<VmAddressRegion> vmar(RootVmar());
  if (!vmar) {
    return nullptr;
  }
  while (1) {
    fbl::RefPtr<VmAddressRegionOrMapping> next(vmar->FindRegion(va));
    if (!next) {
      return vmar;
    }

    if (next->is_mapping()) {
      return next;
    }

    vmar = next->as_vm_address_region();
  }
}

void VmAspace::AttachToThread(Thread* t) {
  canary_.Assert();
  DEBUG_ASSERT(t);

  // point the lk thread at our object
  Guard<MonitoredSpinLock, IrqSave> thread_lock_guard{ThreadLock::Get(), SOURCE_TAG};

  // not prepared to handle setting a new address space or one on a running thread
  DEBUG_ASSERT(!t->aspace());
  DEBUG_ASSERT(t->state() != THREAD_RUNNING);

  t->switch_aspace(this);
}

zx_status_t VmAspace::PageFault(vaddr_t va, uint flags) {
  VM_KTRACE_DURATION(2, "VmAspace::PageFault", va, flags);
  canary_.Assert();
  LTRACEF("va %#" PRIxPTR ", flags %#x\n", va, flags);

  if (type_ == Type::GuestPhysical) {
    flags &= ~VMM_PF_FLAG_USER;
    flags |= VMM_PF_FLAG_GUEST;
  }

  zx_status_t status = ZX_OK;
  __UNINITIALIZED LazyPageRequest page_request;
  do {
    {
      // for now, hold the aspace lock across the page fault operation,
      // which stops any other operations on the address space from moving
      // the region out from underneath it
      Guard<CriticalMutex> guard{&lock_};
      DEBUG_ASSERT(!aspace_destroyed_);
      // First check if we're faulting on the same mapping as last time to short-circuit the vmar
      // walk.
      if (likely(last_fault_ && last_fault_->is_in_range(va, 1))) {
        AssertHeld(last_fault_->lock_ref());
        status = last_fault_->PageFault(va, flags, &page_request);
      } else {
        AssertHeld(root_vmar_->lock_ref());
        status = root_vmar_->PageFault(va, flags, &page_request);
      }
    }

    if (status == ZX_ERR_SHOULD_WAIT) {
      zx_status_t st = page_request->Wait();
      if (st != ZX_OK) {
        if (st == ZX_ERR_TIMED_OUT) {
          Guard<CriticalMutex> guard{&lock_};
          AssertHeld(root_vmar_->lock_ref());
          root_vmar_->DumpLocked(0, false);
        }
        return st;
      }
    }
  } while (status == ZX_ERR_SHOULD_WAIT);

  return status;
}

zx_status_t VmAspace::SoftFault(vaddr_t va, uint flags) {
  // With the current implementation we can just reuse the internal PageFault mechanism.
  return PageFault(va, flags | VMM_PF_FLAG_SW_FAULT);
}

zx_status_t VmAspace::AccessedFault(vaddr_t va) {
  VM_KTRACE_DURATION(2, "VmAspace::AccessedFault", va, 0);
  // There are no permissions etc associated with accessed bits so we can skip any vmar walking and
  // just let the hardware aspace walk for the virtual address.
  // Similar to a page fault, multiple additional pages in the page table will be marked active to
  // amortize the cost of accessed faults. This reduces the accuracy of page age information, at the
  // gain of performance due to reduced number of faults. Given this accessed fault path is meant to
  // just be a fastpath of the page fault path, using the same count and strategy as a page fault at
  // least provides consistency of the trade off of page age accuracy and fault frequency.
  va = ROUNDDOWN(va, PAGE_SIZE);
  const uint64_t next_pt_base = ArchVmAspace::NextUserPageTableOffset(va);
  // Find the minimum between the size of this mapping and the end of the page table.
  const uint64_t max_mark = ktl::min(next_pt_base, base_ + size_);
  // Convert this into a number of pages, limiting to the max lookup pages for consistency with the
  // page fault path.
  const uint64_t max_pages = ktl::min((max_mark - va) / PAGE_SIZE, VmObject::LookupInfo::kMaxPages);
  return arch_aspace_.MarkAccessed(va, max_pages);
}

void VmAspace::Dump(bool verbose) const {
  Guard<CriticalMutex> guard{&lock_};
  DumpLocked(verbose);
}

void VmAspace::DumpLocked(bool verbose) const {
  canary_.Assert();
  printf("as %p [%#" PRIxPTR " %#" PRIxPTR "] sz %#zx typ %u ref %d '%s' destroyed %d\n", this,
         base_, base_ + size_ - 1, size_, static_cast<uint>(type_), ref_count_debug(), name_,
         aspace_destroyed_);

  if (verbose && root_vmar_) {
    AssertHeld(root_vmar_->lock_ref());
    root_vmar_->DumpLocked(1, verbose);
  }
}

zx_status_t VmAspace::EnumerateChildren(VmEnumerator* ve) {
  canary_.Assert();
  DEBUG_ASSERT(ve != nullptr);
  Guard<CriticalMutex> guard{&lock_};
  if (root_vmar_ == nullptr || aspace_destroyed_) {
    // Aspace hasn't been initialized or has already been destroyed.
    return ZX_ERR_BAD_STATE;
  }
  AssertHeld(root_vmar_->lock_ref());
  DEBUG_ASSERT(root_vmar_->IsAliveLocked());
  if (!ve->OnVmAddressRegion(root_vmar_.get(), 0)) {
    return ZX_ERR_CANCELED;
  }
  return root_vmar_->EnumerateChildrenLocked(ve);
}

void VmAspace::DumpAllAspaces(bool verbose) {
  Guard<Mutex> guard{AspaceListLock::Get()};

  for (const auto& a : aspaces_list_) {
    a.Dump(verbose);
  }
}

VmAspace* VmAspace::vaddr_to_aspace(uintptr_t address) {
  if (is_kernel_address(address)) {
    return kernel_aspace();
  } else if (is_user_accessible(address)) {
    return Thread::Current::Get()->aspace();
  } else {
    return nullptr;
  }
}

VmAspace::AslrConfig VmAspace::CreateAslrConfig(Type type) {
  // As documented in //docs/gen/boot-options.md.
  static constexpr uint8_t kMaxAslrEntropy = 36;

  VmAspace::AslrConfig config = {};

  config.enabled = type == Type::User && !gBootOptions->aslr_disabled;
  if (config.enabled) {
    config.entropy_bits = ktl::min(gBootOptions->aslr_entropy_bits, kMaxAslrEntropy);
    config.compact_entropy_bits = 8;
  }

  crypto::global_prng::GetInstance()->Draw(config.seed, sizeof(config.seed));

  return config;
}

void VmAspace::InitializeAslr() {
  aslr_prng_.AddEntropy(aslr_config_.seed, sizeof(aslr_config_.seed));
}

uintptr_t VmAspace::vdso_base_address() const {
  Guard<CriticalMutex> guard{&lock_};
  return VDso::base_address(vdso_code_mapping_);
}

uintptr_t VmAspace::vdso_code_address() const {
  Guard<CriticalMutex> guard{&lock_};
  return vdso_code_mapping_ ? vdso_code_mapping_->base() : 0;
}

void VmAspace::DropAllUserPageTables() {
  Guard<Mutex> guard{AspaceListLock::Get()};

  for (auto& a : aspaces_list_) {
    a.DropUserPageTables();
  }
}

void VmAspace::DropUserPageTables() {
  if (!is_user())
    return;
  Guard<CriticalMutex> guard{&lock_};
  arch_aspace().Unmap(base(), size() / PAGE_SIZE, ArchVmAspace::EnlargeOperation::Yes, nullptr);
}

bool VmAspace::IntersectsVdsoCodeLocked(vaddr_t base, size_t size) const {
  return vdso_code_mapping_ &&
         Intersects(vdso_code_mapping_->base(), vdso_code_mapping_->size(), base, size);
}

bool VmAspace::IsLatencySensitive() const {
  return is_latency_sensitive_.load(ktl::memory_order_relaxed);
}

void VmAspace::MarkAsLatencySensitive() {
  Guard<CriticalMutex> guard{&lock_};
  if (root_vmar_ == nullptr || aspace_destroyed_) {
    // Aspace hasn't been initialized or has already been destroyed.
    return;
  }

  // TODO(fxb/101641): Need a better mechanism than checking for the process name here. See
  // fxbug.dev/85056 for more context.
  char name[ZX_MAX_NAME_LEN];
  if (Thread::Current::Get()->aspace() != this) {
    return;
  }
  ProcessDispatcher* up = ProcessDispatcher::GetCurrent();
  up->get_name(name);
  if (strncmp(name, "audio_core.cm", ZX_MAX_NAME_LEN) != 0 &&
      strncmp(name, "waves_host.cm", ZX_MAX_NAME_LEN) != 0) {
    return;
  }

  bool was_sensitive = is_latency_sensitive_.exchange(true);
  // If this aspace was previously not latency sensitive, then we need to go and tag any VMOs that
  // already have mappings. Although expensive, this only ever needs to be done once for an aspace.
  if (!was_sensitive) {
    vm_aspace_marked_latency_sensitive.Add(1);
    class Enumerator : public VmEnumerator {
     public:
      bool OnVmMapping(const VmMapping* map, const VmAddressRegion* vmar, uint depth) override
          TA_REQ(map->lock()) {
        map->MarkObjectAsLatencySensitiveLocked();
        return true;
      }
    };
    Enumerator enumerator;
    AssertHeld(root_vmar_->lock_ref());
    __UNUSED zx_status_t result = root_vmar_->EnumerateChildrenLocked(&enumerator);
    DEBUG_ASSERT(result == ZX_OK);
  }
}

void VmAspace::HarvestAllUserAccessedBits(NonTerminalAction non_terminal_action,
                                          TerminalAction terminal_action) {
  VM_KTRACE_DURATION(2, "VmAspace::HarvestAllUserAccessedBits");
  Guard<Mutex> guard{AspaceListLock::Get()};

  for (auto& a : aspaces_list_) {
    if (a.is_user()) {
      // TODO(fxb/101641): Formalize this.
      // Forbid PT reclamation and accessed bit harvesting on latency sensitive aspaces.
      const NonTerminalAction apply_non_terminal_action =
          a.IsLatencySensitive() ? NonTerminalAction::Retain : non_terminal_action;
      const TerminalAction apply_terminal_action =
          a.IsLatencySensitive() ? TerminalAction::UpdateAge : terminal_action;
      // The arch_aspace is only destroyed in the VmAspace destructor *after* the aspace is removed
      // from the aspaces list. As we presently hold the AspaceListLock::Get() we know that this
      // destructor has not completed, and so the arch_aspace has not been destroyed. Even if the
      // actual VmAspace has been destroyed, it is still completely safe to walk to the hardware
      // page tables, there just will not be anything there.
      // First we always check ActiveSinceLastCheck (even if we could separately infer that we have
      // to do a harvest) in order to clear the state from it.
      bool harvest = true;
      if (a.arch_aspace().ActiveSinceLastCheck(
              apply_terminal_action == TerminalAction::UpdateAgeAndHarvest ? true : false)) {
        // The aspace has been active since some kind of harvest last happened, so we must do a new
        // one. Reset our counter of how many pt reclamations we've done based on what kind scan
        // this is.
        if (apply_non_terminal_action == NonTerminalAction::FreeUnaccessed) {
          // This is set to one since we haven't yet performed the harvest, and so if next time the
          // call to ActiveSinceLastCheck() returns false, then it will be true that one harvest has
          // been done since last active. Alternative if next time ActiveSinceLastCheck() returns
          // true, then we'll just re-set this back to 1 again.
          a.pt_harvest_since_active_ = 1;
        } else {
          a.pt_harvest_since_active_ = 0;
        }
      } else if (apply_non_terminal_action == NonTerminalAction::FreeUnaccessed &&
                 a.pt_harvest_since_active_ < 2) {
        // The aspace hasn't been active, but we haven't yet performed two successive pt
        // reclamations. Since the first pt reclamation only removes accessed information, the
        // second is needed to actually do the reclamation.
        a.pt_harvest_since_active_++;
      } else {
        // Either this is not a request to harvest pt information, or enough pt harvesting has been
        // done, and so we can skip as the aspace should now be at a fixed point with no new
        // information.
        harvest = false;
      }
      if (harvest) {
        zx_status_t __UNUSED result = a.arch_aspace().HarvestAccessed(
            a.base(), a.size() / PAGE_SIZE, apply_non_terminal_action, apply_terminal_action);
        DEBUG_ASSERT(result == ZX_OK);
        vm_aspace_accessed_harvests_performed.Add(1);
      } else {
        vm_aspace_accessed_harvests_skipped.Add(1);
      }
    }
  }
}
