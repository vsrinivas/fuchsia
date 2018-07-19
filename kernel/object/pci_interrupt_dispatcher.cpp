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
                                                           uint irq_id, void* ctx) {
    DEBUG_ASSERT(ctx);
    auto thiz = reinterpret_cast<PciInterruptDispatcher*>(ctx);
    thiz->InterruptHandler();
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
    auto interrupt_dispatcher = new (&ac) PciInterruptDispatcher(device, irq_id, maskable);
    fbl::RefPtr<Dispatcher> dispatcher = fbl::AdoptRef<Dispatcher>(interrupt_dispatcher);
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    Guard<fbl::Mutex> guard{interrupt_dispatcher->get_lock()};

    interrupt_dispatcher->set_flags(INTERRUPT_UNMASK_PREWAIT);

    // Register the interrupt
    zx_status_t status = interrupt_dispatcher->RegisterInterruptHandler();
    if (status != ZX_OK)
        return status;

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

void PciInterruptDispatcher::MaskInterrupt() {
    if (maskable_)
        device_->MaskIrq(vector_);
}

void PciInterruptDispatcher::UnmaskInterrupt() {
    if (maskable_)
        device_->UnmaskIrq(vector_);
}

zx_status_t PciInterruptDispatcher::RegisterInterruptHandler() {
    return device_->RegisterIrqHandler(vector_, IrqThunk, this);
}

void PciInterruptDispatcher::UnregisterInterruptHandler() {
    device_->RegisterIrqHandler(vector_, nullptr, nullptr);
}

#endif  // if WITH_DEV_PCIE
