// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#if WITH_DEV_PCIE

#include <object/pci_interrupt_dispatcher.h>

#include <kernel/auto_lock.h>
#include <magenta/rights.h>
#include <fbl/alloc_checker.h>
#include <object/pci_device_dispatcher.h>

PciInterruptDispatcher::~PciInterruptDispatcher() {
    if (device_) {
        // Unregister our handler.
        __UNUSED mx_status_t ret;
        ret = device_->RegisterIrqHandler(irq_id_, nullptr, nullptr);
        DEBUG_ASSERT(ret == MX_OK);  // This should never fail.

        // Release our reference to our device.
        device_ = nullptr;
    }
}

pcie_irq_handler_retval_t PciInterruptDispatcher::IrqThunk(const PcieDevice& dev,
                                                           uint irq_id,
                                                           void* ctx) {
    DEBUG_ASSERT(ctx);
    PciInterruptDispatcher* thiz = (PciInterruptDispatcher*)ctx;

    // Mask the IRQ at the PCIe hardware level if we can, and (if any threads
    // just became runable) tell the kernel to trigger a reschedule event.
    if (thiz->signal() > 0) {
        return PCIE_IRQRET_MASK_AND_RESCHED;
    } else {
        return PCIE_IRQRET_MASK;
    }
}

mx_status_t PciInterruptDispatcher::Create(
        const fbl::RefPtr<PcieDevice>& device,
        uint32_t irq_id,
        bool maskable,
        mx_rights_t* out_rights,
        fbl::RefPtr<Dispatcher>* out_interrupt) {
    // Sanity check our args
    if (!device || !out_rights || !out_interrupt) {
        return MX_ERR_INVALID_ARGS;
    }

    fbl::AllocChecker ac;
    // Attempt to allocate a new dispatcher wrapper.
    auto interrupt_dispatcher = new (&ac) PciInterruptDispatcher(irq_id, maskable);
    fbl::RefPtr<Dispatcher> dispatcher = fbl::AdoptRef<Dispatcher>(interrupt_dispatcher);
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    // Stash reference to the underlying device in the dispatcher we just
    // created, then attempt to register our dispatcher with the bus driver.
    DEBUG_ASSERT(device);
    interrupt_dispatcher->device_ = device;
    mx_status_t result = device->RegisterIrqHandler(irq_id,
                                                           IrqThunk,
                                                           interrupt_dispatcher);
    if (result != MX_OK) {
        interrupt_dispatcher->device_ = nullptr;
        return result;
    }

    // Everything seems to have gone well.  Make sure the interrupt is unmasked
    // (if it is maskable) then transfer our dispatcher refererence to the
    // caller.
    if (maskable) {
        device->UnmaskIrq(irq_id);
    }
    *out_interrupt = fbl::move(dispatcher);
    *out_rights    = MX_DEFAULT_PCI_INTERRUPT_RIGHTS;
    return MX_OK;
}

mx_status_t PciInterruptDispatcher::InterruptComplete() {
    DEBUG_ASSERT(device_ != nullptr);
    unsignal();

    if (maskable_)
        device_->UnmaskIrq(irq_id_);

    return MX_OK;
}

mx_status_t PciInterruptDispatcher::UserSignal() {
    DEBUG_ASSERT(device_ != nullptr);

    if (maskable_)
        device_->MaskIrq(irq_id_);

    signal(true);

    return MX_OK;
}

#endif  // if WITH_DEV_PCIE
