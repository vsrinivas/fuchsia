// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/counters.h>
#include <platform.h>
#include <trace.h>
#include <zircon/rights.h>
#include <zircon/types.h>

#include <array>
#include <cstring>

#include <arch/arch_ops.h>
#include <dev/interrupt.h>
#include <fbl/alloc_checker.h>
#include <kernel/auto_lock.h>
#include <object/dispatcher.h>
#include <object/interrupt_dispatcher.h>
#include <object/msi_dispatcher.h>
#include <vm/vm_address_region.h>
#include <vm/vm_object.h>

#define LOCAL_TRACE 0

KCOUNTER(dispatcher_msi_create_count, "msi_dispatcher.create")
KCOUNTER(dispatcher_msi_interrupt_count, "msi_dispatcher.interrupts")
KCOUNTER(dispatcher_msi_mask_count, "msi_dispatcher.mask")
KCOUNTER(dispatcher_msi_unmask_count, "msi_dispatcher.unmask")
KCOUNTER(dispatcher_msi_destroy_count, "msi_dispatcher.destroy")

// Creates an a derived MsiDispatcher determined by the flags passed in
zx_status_t MsiDispatcher::Create(fbl::RefPtr<MsiAllocation> alloc, uint32_t msi_id,
                                  const fbl::RefPtr<VmObject>& vmo, zx_paddr_t reg_offset,
                                  uint32_t flags, zx_rights_t* out_rights,
                                  KernelHandle<InterruptDispatcher>* out_interrupt,
                                  RegisterIntFn register_int_fn, bool test_interrupt) {
  if (!out_rights || !out_interrupt) {
    LTRACEF("Invalid MSI parameters\n");
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t base_irq_id = 0;
  {
    Guard<SpinLock, IrqSave> guard{&alloc->lock()};
    if (msi_id >= alloc->block().num_irq) {
      LTRACEF("msi_id %u is out of range for the block (num_irqs: %u)\n", msi_id,
              alloc->block().num_irq);
      return ZX_ERR_BAD_STATE;
    }
    base_irq_id = alloc->block().base_irq_id;
  }

  auto cleanup = fbl::MakeAutoCall([alloc, msi_id]() { alloc->ReleaseId(msi_id); });
  zx_status_t st = alloc->ReserveId(msi_id);
  if (st != ZX_OK) {
    return st;
  }

  // To handle MSI masking we need to create a kernel mapping for the VMO handed
  // to us, this will provide access to the register controlling the given MSI.
  // The VMO must be a contiguous VMO with the cache policy already configured.
  auto vmar = VmAspace::kernel_aspace()->RootVmar();
  if (vmo->GetMappingCachePolicy() != ZX_CACHE_POLICY_UNCACHED_DEVICE || !vmo->is_contiguous()) {
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t vector = base_irq_id + msi_id;
  std::array<char, ZX_MAX_NAME_LEN> name{};
  snprintf(name.data(), name.max_size(), "msi id %u (vector %u)", msi_id, vector);
  fbl::RefPtr<VmMapping> mapping;
  auto size = vmo->size();
  st = vmar->CreateVmMapping(0, size, 0, 0, vmo, 0,
                             ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE, name.data(),
                             &mapping);
  if (st != ZX_OK) {
    LTRACEF("Failed to create MSI mapping: %d\n", st);
    return st;
  }

  st = mapping->MapRange(0, size, true);
  if (st != ZX_OK) {
    LTRACEF("Falled to MapRange for the mapping: %d\n", st);
    return st;
  }

  // For the moment we only support MSI, but when MSI-X is added this object creation will
  // be extended to return a derived type suitable for MSI-X operation.
  fbl::AllocChecker ac;
  fbl::RefPtr<MsiDispatcher> disp = fbl::AdoptRef<MsiDispatcher>(
      new (&ac) MsiDispatcherImpl(ktl::move(alloc), base_irq_id, msi_id, ktl::move(mapping),
                                  reg_offset, flags & MSI_FLAG_HAS_PVM, register_int_fn));
  if (!ac.check()) {
    LTRACEF("Failed to allocate MsiDispatcher\n");
    return ZX_ERR_NO_MEMORY;
  }
  // If we allocated MsiDispatcher successfully then its dtor will release
  // the id if necessary.
  cleanup.cancel();

  // MSI / MSI-X interrupts share a masking approach and should be masked while
  // being serviced and unmasked while waiting for an interrupt message to arrive.
  disp->set_flags(INTERRUPT_UNMASK_PREWAIT | INTERRUPT_MASK_POSTWAIT |
                  ((test_interrupt) ? INTERRUPT_VIRTUAL : 0));

  // Mask the interrupt until it is needed.
  disp->MaskInterrupt();
  st = disp->RegisterInterruptHandler();
  if (st != ZX_OK) {
    LTRACEF("Failed to register interrupt handler for msi id %u (vector %u): %d\n", msi_id, vector,
            st);
    return st;
  }

  *out_rights = default_rights();
  out_interrupt->reset(ktl::move(disp));
  LTRACEF("MsiDispatcher successfully created.\n");
  return ZX_OK;
}

MsiDispatcher::MsiDispatcher(fbl::RefPtr<MsiAllocation>&& alloc, fbl::RefPtr<VmMapping>&& mapping,
                             uint32_t base_irq_id, uint32_t msi_id, RegisterIntFn register_int_fn)
    : alloc_(ktl::move(alloc)),
      mapping_(ktl::move(mapping)),
      register_int_fn_(register_int_fn),
      base_irq_id_(base_irq_id),
      msi_id_(msi_id) {
  kcounter_add(dispatcher_msi_create_count, 1);
}

MsiDispatcher::~MsiDispatcher() {
  zx_status_t st = alloc_->ReleaseId(msi_id_);
  if (st != ZX_OK) {
    printf("MsiDispatcher: Failed to release MSI id %u (vector %u): %d\n", msi_id_,
           base_irq_id_ + msi_id_, st);
  }
  kcounter_add(dispatcher_msi_destroy_count, 1);
}

// This IrqHandler acts as a trampoline to call into the base
// InterruptDispatcher's InterruptHandler() routine. Masking and signaling will
// be handled there based on flags set in the constructor.
interrupt_eoi MsiDispatcher::IrqHandler(void* ctx) {
  auto self = reinterpret_cast<MsiDispatcher*>(ctx);
  self->InterruptHandler();
  kcounter_add(dispatcher_msi_interrupt_count, 1);
  return IRQ_EOI_DEACTIVATE;
}

zx_status_t MsiDispatcher::RegisterInterruptHandler() {
  Guard<SpinLock, IrqSave> guard{&alloc_->lock()};
  register_int_fn_(&alloc_->block(), msi_id_, IrqHandler, this);
  return ZX_OK;
}

void MsiDispatcher::UnregisterInterruptHandler() {
  Guard<SpinLock, IrqSave> guard{&alloc_->lock()};
  register_int_fn_(&alloc_->block(), msi_id_, nullptr, this);
}

void MsiDispatcherImpl::MaskInterrupt() {
  kcounter_add(dispatcher_msi_mask_count, 1);

  Guard<SpinLock, IrqSave> guard{&allocation()->lock()};
  if (platform_pvm_supported_) {
    msi_mask_unmask(&allocation()->block(), msi_id(), true);
  }

  if (capability_pvm_supported_) {
    *mask_reg_ |= (1 << msi_id());
    mb();
  }
}

void MsiDispatcherImpl::UnmaskInterrupt() {
  kcounter_add(dispatcher_msi_unmask_count, 1);

  Guard<SpinLock, IrqSave> guard{&allocation()->lock()};
  if (platform_pvm_supported_) {
    msi_mask_unmask(&allocation()->block(), msi_id(), false);
  }

  if (capability_pvm_supported_) {
    *mask_reg_ &= ~(1 << msi_id());
    mb();
  }
}
