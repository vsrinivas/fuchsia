// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <new.h>

#include <kernel/auto_lock.h>
#include <magenta/pci_device_dispatcher.h>
#include <magenta/pci_interrupt_dispatcher.h>

constexpr mx_rights_t kDefaultPciInterruptRights = MX_RIGHT_READ |
                                                   MX_RIGHT_WRITE |
                                                   MX_RIGHT_TRANSFER;

PciInterruptDispatcher::~PciInterruptDispatcher() {
    if (device_) {
        // Unregister our handler.
        __UNUSED status_t ret;
        ret = pcie_register_irq_handler(device_->device(), irq_id_, NULL, NULL);
        DEBUG_ASSERT(ret == NO_ERROR);  // This should never fail.

        // Release our reference to our device.
        device_ = nullptr;
    }
}

pcie_irq_handler_retval_t PciInterruptDispatcher::IrqThunk(struct pcie_device_state* dev,
                                                           uint irq_id,
                                                           void* ctx) {
    DEBUG_ASSERT(ctx);
    PciInterruptDispatcher* thiz = (PciInterruptDispatcher*)ctx;

    // Mask the IRQ at the PCIe hardware level if we can, and (if any threads
    // just became runable) tell the kernel to trigger a reschedule event.
    return thiz->state_tracker_.UpdateSatisfiedFromIrq(0u, MX_SIGNAL_SIGNALED)
        ? PCIE_IRQRET_MASK_AND_RESCHED
        : PCIE_IRQRET_MASK;
}

status_t PciInterruptDispatcher::Create(
        const mxtl::RefPtr<PciDeviceDispatcher::PciDeviceWrapper>& device,
        uint32_t irq_id,
        bool maskable,
        mx_rights_t* out_rights,
        mxtl::RefPtr<Dispatcher>* out_interrupt) {
    // Sanity check our args
    if (!device || !out_rights || !out_interrupt)
        return ERR_INVALID_ARGS;

    AllocChecker ac;
    // Attempt to allocate a new dispatcher wrapper.
    auto interrupt_dispatcher = new (&ac) PciInterruptDispatcher(irq_id, maskable);
    mxtl::RefPtr<Dispatcher> dispatcher = mxtl::AdoptRef<Dispatcher>(interrupt_dispatcher);
    if (!ac.check())
        return ERR_NO_MEMORY;

    // Stash reference to the underlying device in the dispatcher we just
    // created, then attempt to register our dispatcher with the bus driver.
    DEBUG_ASSERT(device->device());
    interrupt_dispatcher->device_ = device;
    status_t result = pcie_register_irq_handler(device->device(),
                                                irq_id,
                                                IrqThunk,
                                                interrupt_dispatcher);
    if (result != NO_ERROR) {
        interrupt_dispatcher->device_ = nullptr;
        return result;
    }

    // Everything seems to have gone well.  Make sure the interrupt is unmasked
    // (if it is maskable) then transfer our dispatcher refererence to the
    // caller.
    if (maskable)
        pcie_unmask_irq(device->device(), irq_id);
    *out_interrupt = mxtl::move(dispatcher);
    *out_rights    = kDefaultPciInterruptRights;
    return NO_ERROR;
}

status_t PciInterruptDispatcher::InterruptComplete() {
    DEBUG_ASSERT(device_ != nullptr);
    state_tracker_.UpdateSatisfied(MX_SIGNAL_SIGNALED, 0u);

    if (maskable_)
        pcie_unmask_irq(device_->device(), irq_id_);

    return NO_ERROR;
}
