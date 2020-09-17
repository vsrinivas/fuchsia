// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/intrin.h>
#include <lib/counters.h>
#include <platform.h>
#include <reg.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/limits.h>
#include <zircon/rights.h>
#include <zircon/types.h>

#include <array>
#include <cstring>
#include <limits>

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
                                  const fbl::RefPtr<VmObject>& vmo, zx_paddr_t vmo_offset,
                                  uint32_t options, zx_rights_t* out_rights,
                                  KernelHandle<InterruptDispatcher>* out_interrupt,
                                  RegisterIntFn register_int_fn) {
  LTRACEF(
      "out_rights = %p, out_interrupt = %p\nvmo: %s, %s, %s\nsize = %lu, "
      "vmo_offset = %lu, options = %#x, cache policy = %u\n",
      out_rights, out_interrupt, vmo->is_paged() ? "paged" : "physical",
      vmo->is_contiguous() ? "contiguous" : "not contiguous",
      vmo->is_resizable() ? "resizable" : "not resizable", vmo->size(), vmo_offset, options,
      vmo->GetMappingCachePolicy());

  bool is_msix = (options & ZX_MSI_MODE_MSI_X) == ZX_MSI_MODE_MSI_X;
  options &= ~ZX_MSI_MODE_MSI_X;

  if (!out_rights || !out_interrupt ||
      (vmo->is_paged() && (vmo->is_resizable() || !vmo->is_contiguous())) ||
      vmo_offset >= vmo->size() || options ||
      vmo->GetMappingCachePolicy() != ZX_CACHE_POLICY_UNCACHED_DEVICE) {
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t base_irq_id = 0;
  {
    Guard<SpinLock, IrqSave> guard{&alloc->lock()};
    if (msi_id >= alloc->block().num_irq) {
      LTRACEF("msi_id %u is out of range for the block (num_irqs: %u)\n", msi_id,
              alloc->block().num_irq);
      return ZX_ERR_INVALID_ARGS;
    }
    base_irq_id = alloc->block().base_irq_id;
  }

  zx_status_t st = alloc->ReserveId(msi_id);
  if (st != ZX_OK) {
    LTRACEF("failed to reserve msi_id %u: %d\n", msi_id, st);
    return st;
  }
  auto cleanup = fbl::MakeAutoCall([alloc, msi_id]() { alloc->ReleaseId(msi_id); });

  // To handle MSI masking we need to create a kernel mapping for the VMO handed
  // to us, this will provide access to the register controlling the given MSI.
  // The VMO must be a contiguous VMO with the cache policy already configured.
  // Size checks will come into play when we know what type of capability we're
  // working with.
  auto vmar = VmAspace::kernel_aspace()->RootVmar();
  uint32_t vector = base_irq_id + msi_id;
  ktl::array<char, ZX_MAX_NAME_LEN> name{};
  snprintf(name.data(), name.max_size(), "msi id %u (vector %u)", msi_id, vector);
  fbl::RefPtr<VmMapping> mapping;
  st = vmar->CreateVmMapping(0, vmo->size(), 0, 0, vmo, 0,
                             ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE, name.data(),
                             &mapping);
  if (st != ZX_OK) {
    LTRACEF("Failed to create MSI mapping: %d\n", st);
    return st;
  }

  st = mapping->MapRange(0, vmo->size(), true);
  if (st != ZX_OK) {
    LTRACEF("Falled to MapRange for the mapping: %d\n", st);
    return st;
  }

  LTRACEF("Mapping mapped at %#lx, size %zx, vmo size %lx, vmo_offset = %#lx\n", mapping->base(),
          mapping->size(), vmo->size(), vmo_offset);
  fbl::AllocChecker ac;
  fbl::RefPtr<MsiDispatcher> disp;

  // MSI lives inside a device's config space within an MSI Capability. MSI-X has a similar
  // capability, but has another table mapped elsewhere which contains individually maskable bits
  // per vector. The capability itself is managed by the PCI bus driver, and the mask bits are
  // handled by this dispatcher. So in the event of MSI-X there is no capability id to check, since
  // we don't touch the capability at all at this level.
  size_t add_result = 0;
  if (is_msix) {
    // Most validation for MSI-X is done in the PCI driver since it can confirm that the Table
    // Structure is appropriately large for the number of interrupts, and the allocation by now has
    // already been made.
    if (add_overflow(vmo_offset, (msi_id + 1) * sizeof(MsixTableEntry), &add_result) ||
        add_result > vmo->size()) {
      return ZX_ERR_INVALID_ARGS;
    }

    disp = fbl::AdoptRef<MsiDispatcher>(new (&ac) MsixDispatcherImpl(
        ktl::move(alloc), base_irq_id, msi_id, ktl::move(mapping), vmo_offset, register_int_fn));
  } else {
    auto* cap = reinterpret_cast<MsiCapability*>(mapping->base() + vmo_offset);
    if (cap->id != kMsiCapabilityId) {
      return ZX_ERR_INVALID_ARGS;
    }

    // MSI capabilities fit within a given device's configuration space which is either 256
    // or 4096 bytes. But in most cases the VMO containing config space is going to be at
    // least the size of a full PCI bus's worth of devices, and physical VMOs cannot be sliced
    // into children. We can validate that the capability fits within the offset given, but
    // otherwise cannot rely on the VMO's size for validation.
    if (add_overflow(vmo_offset, sizeof(MsiCapability), &add_result) || add_result > vmo->size()) {
      return ZX_ERR_INVALID_ARGS;
    }

    uint16_t ctrl_val = cap->control;
    bool has_pvm = !!(ctrl_val & kMsiPvmSupported);
    bool has_64bit = !!(ctrl_val & kMsi64bitSupported);
    disp = fbl::AdoptRef<MsiDispatcher>(
        new (&ac) MsiDispatcherImpl(ktl::move(alloc), base_irq_id, msi_id, ktl::move(mapping),
                                    vmo_offset, has_pvm, has_64bit, register_int_fn));
  }

  if (!ac.check()) {
    LTRACEF("Failed to allocate MsiDispatcher\n");
    return ZX_ERR_NO_MEMORY;
  }
  // If we allocated MsiDispatcher successfully then its dtor will release
  // the id if necessary.
  cleanup.cancel();

  // MSI / MSI-X interrupts share a masking approach and should be masked while
  // being serviced and unmasked while waiting for an interrupt message to arrive.
  disp->set_flags(INTERRUPT_UNMASK_PREWAIT | INTERRUPT_MASK_POSTWAIT);

  disp->UnmaskInterrupt();
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
    LTRACEF("MsiDispatcher: Failed to release MSI id %u (vector %u): %d\n", msi_id_,
            base_irq_id_ + msi_id_, st);
  }
  LTRACEF("MsiDispatcher: cleaning up MSI id %u\n", msi_id_);
  kcounter_add(dispatcher_msi_destroy_count, 1);
}

// This IrqHandler acts as a trampoline to call into the base
// InterruptDispatcher's InterruptHandler() routine. Masking and signaling will
// be handled there based on flags set in the constructor.
interrupt_eoi MsiDispatcher::IrqHandler(void* ctx) {
  auto* self = reinterpret_cast<MsiDispatcher*>(ctx);
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
  if (has_platform_pvm_) {
    msi_mask_unmask(&allocation()->block(), msi_id(), true);
  }

  if (has_cap_pvm_) {
    *mask_bits_reg_ |= (1 << msi_id());
    arch::DeviceMemoryBarrier();
  }
}

void MsiDispatcherImpl::UnmaskInterrupt() {
  kcounter_add(dispatcher_msi_unmask_count, 1);

  Guard<SpinLock, IrqSave> guard{&allocation()->lock()};
  if (has_platform_pvm_) {
    msi_mask_unmask(&allocation()->block(), msi_id(), false);
  }

  if (has_cap_pvm_) {
    *mask_bits_reg_ &= ~(1 << msi_id());
    arch::DeviceMemoryBarrier();
  }
}

MsixDispatcherImpl::MsixDispatcherImpl(fbl::RefPtr<MsiAllocation>&& alloc, uint32_t base_irq_id,
                                       uint32_t msi_id, fbl::RefPtr<VmMapping>&& mapping,
                                       zx_off_t table_offset, RegisterIntFn register_int_fn)
    : MsiDispatcher(ktl::move(alloc), ktl::move(mapping), base_irq_id, msi_id, register_int_fn),
      table_entries_(reinterpret_cast<MsixTableEntry*>(this->mapping()->base() + table_offset)) {
  // Disable the vector, set up the address and data registers, then re-enable
  // it for our given msi_id. Per PCI Local Bus Spec v3 section 6.8.2
  // implementation notes, all accesses to these registers must be DWORD or
  // QWORD only. We write upper and lower halves of the address unconditionally
  // because if the address is 32 bits then we want to write zeroes to the upper
  // half regardless. The msg_data field is incremented by msi_id because unlike
  // MSI, MSI-X does not adjust the data payload. This allows us to point
  // multiple table entries at the same vector, but requires us to specify the
  // vector in the data field.
  MaskInterrupt();
  writel(allocation()->block().tgt_addr & UINT32_MAX, &table_entries_[msi_id].msg_addr);
  writel(static_cast<uint32_t>(allocation()->block().tgt_addr >> 32),
         &table_entries_[msi_id].msg_upper_addr);
  writel(allocation()->block().tgt_data + msi_id, &table_entries_[msi_id].msg_data);
  arch::DeviceMemoryBarrier();
}

void MsixDispatcherImpl::MaskInterrupt() {
  kcounter_add(dispatcher_msi_mask_count, 1);
  RMWREG32(&table_entries_[msi_id()].vector_control, kMsixVectorControlMaskBit, 1, 1);
  arch::DeviceMemoryBarrier();
}

void MsixDispatcherImpl::UnmaskInterrupt() {
  kcounter_add(dispatcher_msi_unmask_count, 1);
  RMWREG32(&table_entries_[msi_id()].vector_control, kMsixVectorControlMaskBit, 1, 0);
  arch::DeviceMemoryBarrier();
}

MsixDispatcherImpl::~MsixDispatcherImpl() {
  MaskInterrupt();
  writel(0, &table_entries_[msi_id()].msg_addr);
  writel(0, &table_entries_[msi_id()].msg_upper_addr);
  writel(0, &table_entries_[msi_id()].msg_data);
  arch::DeviceMemoryBarrier();
}
