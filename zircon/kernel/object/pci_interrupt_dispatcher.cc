// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "arch/ops.h"
#if WITH_KERNEL_PCIE

#include <lib/counters.h>
#include <platform.h>
#include <zircon/rights.h>

#include <fbl/alloc_checker.h>
#include <kernel/auto_lock.h>
#include <object/interrupt_dispatcher.h>
#include <object/pci_device_dispatcher.h>
#include <object/pci_interrupt_dispatcher.h>

KCOUNTER(dispatcher_pci_interrupt_create_count, "dispatcher.pci_interrupt.create")
KCOUNTER(dispatcher_pci_interrupt_destroy_count, "dispatcher.pci_interrupt.destroy")

PciInterruptDispatcher::~PciInterruptDispatcher() {
  kcounter_add(dispatcher_pci_interrupt_destroy_count, 1);

  // Release our reference to our device.
  device_ = nullptr;
}

pcie_irq_handler_retval_t PciInterruptDispatcher::IrqThunk(const PcieDevice& dev, uint irq_id,
                                                           void* ctx) {
  DEBUG_ASSERT(ctx);
  auto thiz = reinterpret_cast<PciInterruptDispatcher*>(ctx);
  thiz->InterruptHandler();
  return PCIE_IRQRET_MASK;
}

zx_status_t PciInterruptDispatcher::Create(const fbl::RefPtr<PcieDevice>& device, uint32_t irq_id,
                                           bool maskable, zx_rights_t* out_rights,
                                           KernelHandle<InterruptDispatcher>* out_interrupt) {
  // Sanity check our args
  if (!device || !out_rights || !out_interrupt) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (!is_valid_interrupt(irq_id, 0)) {
    return ZX_ERR_INTERNAL;
  }

  // Attempt to allocate a new dispatcher wrapper.
  // Do not create a KernelHandle until all initialization has succeeded;
  // if an interrupt already exists on |vector| our on_zero_handles() would
  // tear down the existing interrupt when creation fails.
  fbl::AllocChecker ac;
  auto interrupt_dispatcher =
      fbl::AdoptRef(new (&ac) PciInterruptDispatcher(device, irq_id, maskable));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  Guard<fbl::Mutex> guard{interrupt_dispatcher->get_lock()};

  // The PcieDevice class contains a mutex that guards device access and can be
  // contended between the PciInterruptDispatcher and the protocol methods used
  // by the drivers downstream. For safe locking & scheduling considerations we
  // need to ensure the InterruptDispatcher's spinlock is not held when calling
  // into this dispatcher to unmask an interrupt. Masking is handled by the pci
  // bus driver itself during operation.
  zx_status_t status = interrupt_dispatcher->set_flags(INTERRUPT_UNMASK_PREWAIT_UNLOCKED);
  if (status != ZX_OK) {
    return status;
  }

  // Register the interrupt
  status = interrupt_dispatcher->RegisterInterruptHandler();
  if (status != ZX_OK) {
    return status;
  }

  // Everything seems to have gone well.  Make sure the interrupt is unmasked
  // (if it is maskable) then transfer our dispatcher reference to the
  // caller.
  if (maskable) {
    device->UnmaskIrq(irq_id);
  }
  out_interrupt->reset(ktl::move(interrupt_dispatcher));
  *out_rights = ZX_DEFAULT_PCI_INTERRUPT_RIGHTS;
  return ZX_OK;
}

// This is only called in the InterruptDispatcher::Destroy() path which does not
// hold the InterruptDispatcher spinlock. The interrupt is masked before the
// interrupt handler is unregistered and the InterruptDispatcher is freed.
void PciInterruptDispatcher::MaskInterrupt() {
  DEBUG_ASSERT(arch_num_spinlocks_held() == 0);
  // MaskInterrupt should never be called by the InterruptDispatcher.
  if (maskable_) {
    device_->MaskIrq(vector_);
  }
}

void PciInterruptDispatcher::UnmaskInterrupt() {
  DEBUG_ASSERT(arch_num_spinlocks_held() == 0);
  if (maskable_) {
    device_->UnmaskIrq(vector_);
  }
}

PciInterruptDispatcher::PciInterruptDispatcher(const fbl::RefPtr<PcieDevice>& device,
                                               uint32_t vector, bool maskable)
    : device_(device), vector_(vector), maskable_(maskable) {
  kcounter_add(dispatcher_pci_interrupt_create_count, 1);
}

zx_status_t PciInterruptDispatcher::RegisterInterruptHandler() {
  return device_->RegisterIrqHandler(vector_, IrqThunk, this);
}

void PciInterruptDispatcher::UnregisterInterruptHandler() {
  device_->RegisterIrqHandler(vector_, nullptr, nullptr);
}

#endif  // if WITH_KERNEL_PCIE
