// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#if WITH_DEV_PCIE

#include <object/pci_interrupt_dispatcher.h>

#include <kernel/auto_lock.h>
#include <zircon/rights.h>
#include <fbl/alloc_checker.h>
#include <object/pci_device_dispatcher.h>
#include <platform.h>

PciInterruptDispatcher::~PciInterruptDispatcher() {
    // Release our reference to our device.
    device_ = nullptr;
}

pcie_irq_handler_retval_t PciInterruptDispatcher::IrqThunk(const PcieDevice& dev,
                                                           uint irq_id,
                                                           void* ctx) {
    DEBUG_ASSERT(ctx);

    Interrupt* interrupt = reinterpret_cast<Interrupt*>(ctx);

    // only record timestamp if this is the first IRQ since we started waiting
    zx_time_t zero_timestamp = 0;
    atomic_cmpxchg_u64(&interrupt->timestamp, &zero_timestamp, current_time());

    PciInterruptDispatcher* thiz
            = reinterpret_cast<PciInterruptDispatcher *>(interrupt->dispatcher);

    // Mask the IRQ at the PCIe hardware level if we can.
    thiz->Signal(SIGNAL_MASK(interrupt->slot), true);
    return PCIE_IRQRET_MASK;
}

zx_status_t PciInterruptDispatcher::Create(
        const fbl::RefPtr<PcieDevice>& device,
        uint32_t irq_id,
        bool maskable,
        zx_rights_t* out_rights,
        fbl::RefPtr<Dispatcher>* out_interrupt) {
    // Sanity check our args
    if (!device || !out_rights || !out_interrupt) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (!is_valid_interrupt(irq_id, 0)) {
        return ZX_ERR_INTERNAL;
    }

    fbl::AllocChecker ac;
    // Attempt to allocate a new dispatcher wrapper.
    auto interrupt_dispatcher = new (&ac) PciInterruptDispatcher(device, maskable);
    fbl::RefPtr<Dispatcher> dispatcher = fbl::AdoptRef<Dispatcher>(interrupt_dispatcher);
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    fbl::AutoLock lock(interrupt_dispatcher->get_lock());

    // bind our PCI interrupt
    zx_status_t result = interrupt_dispatcher->AddSlotLocked(ZX_PCI_INTERRUPT_SLOT, irq_id,
                                                             INTERRUPT_UNMASK_PREWAIT);
    if (result != ZX_OK)
        return result;

    // prebind ZX_INTERRUPT_SLOT_USER
    result = interrupt_dispatcher->AddSlotLocked(ZX_INTERRUPT_SLOT_USER, 0, INTERRUPT_VIRTUAL);
    if (result != ZX_OK)
        return result;


    // Everything seems to have gone well.  Make sure the interrupt is unmasked
    // (if it is maskable) then transfer our dispatcher refererence to the
    // caller.
    if (maskable) {
        device->UnmaskIrq(irq_id);
    }
    *out_interrupt = fbl::move(dispatcher);
    *out_rights    = ZX_DEFAULT_PCI_INTERRUPT_RIGHTS;
    return ZX_OK;
}

zx_status_t PciInterruptDispatcher::Bind(uint32_t slot, uint32_t vector, uint32_t options) {
    canary_.Assert();

    if (slot > ZX_INTERRUPT_MAX_SLOTS)
        return ZX_ERR_INVALID_ARGS;

    // For PCI interrupt handles we only support binding virtual interrupts
    if (options != ZX_INTERRUPT_VIRTUAL)
        return ZX_ERR_INVALID_ARGS;

    fbl::AutoLock lock(get_lock());

    return AddSlotLocked(slot, vector, INTERRUPT_VIRTUAL);
}

void PciInterruptDispatcher::MaskInterrupt(uint32_t vector) {
    if (maskable_)
        device_->MaskIrq(vector);
}

void PciInterruptDispatcher::UnmaskInterrupt(uint32_t vector) {
    if (maskable_)
        device_->UnmaskIrq(vector);
}

zx_status_t PciInterruptDispatcher::RegisterInterruptHandler(uint32_t vector, void* data) {
    return device_->RegisterIrqHandler(vector, IrqThunk, data);
}

void PciInterruptDispatcher::UnregisterInterruptHandler(uint32_t vector) {
    device_->RegisterIrqHandler(vector, nullptr, nullptr);
}

#endif  // if WITH_DEV_PCIE
