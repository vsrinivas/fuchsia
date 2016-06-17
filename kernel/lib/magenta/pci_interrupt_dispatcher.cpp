// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <kernel/auto_lock.h>
#include <magenta/pci_device_dispatcher.h>
#include <magenta/pci_interrupt_dispatcher.h>

constexpr mx_rights_t kDefaultPciInterruptRights = MX_RIGHT_READ | MX_RIGHT_TRANSFER;

PciInterruptDispatcher::PciInterruptDispatcher(uint32_t irq_id)
    : irq_id_(irq_id) {
    mutex_init(&wait_lock_);
    mutex_init(&lock_);
    event_init(&event_, false, EVENT_FLAG_AUTOUNSIGNAL);
}

PciInterruptDispatcher::~PciInterruptDispatcher() {
    event_destroy(&event_);
    mutex_destroy(&lock_);
    mutex_destroy(&wait_lock_);
}

pcie_irq_handler_retval_t PciInterruptDispatcher::IrqThunk(struct pcie_common_state* dev,
                                                           uint irq_id,
                                                           void* ctx) {
    DEBUG_ASSERT(ctx);
    PciInterruptDispatcher* dispatcher = (PciInterruptDispatcher*)ctx;

    // Wake up any thread which has been waiting for us to fire.
    event_signal(&dispatcher->event_, false);

    // Mask the IRQ at the PCIe hardware level if we can, and tell the kernel to
    // trigger a reschedule event.
    return PCIE_IRQRET_MASK_AND_RESCHED;
}

status_t PciInterruptDispatcher::Create(
        const utils::RefPtr<PciDeviceDispatcher::PciDeviceWrapper>& device,
        uint32_t irq_id,
        bool maskable,
        mx_rights_t* out_rights,
        utils::RefPtr<Dispatcher>* out_interrupt) {
    // Sanity check our args
    if (!device || !out_rights || !out_interrupt)
        return ERR_INVALID_ARGS;

    // Attempt to allocate a new dispatcher wrapper.
    PciInterruptDispatcher*   interrupt_dispatcher = new PciInterruptDispatcher(irq_id);
    utils::RefPtr<Dispatcher> dispatcher = utils::AdoptRef<Dispatcher>(interrupt_dispatcher);
    if (!dispatcher)
        return ERR_NO_MEMORY;

    // Attempt to register our dispatcher with the bus driver.
    DEBUG_ASSERT(device->device());
    status_t result = pcie_register_irq_handler(&device->device()->common,
                                                irq_id,
                                                IrqThunk,
                                                interrupt_dispatcher);
    if (result != NO_ERROR)
        return result;

    // Everything seems to have gone well.  Stash reference to the underlying
    // device in the dispatcher we just created, then transfer our reference to
    // the dispatcher out parameter.
    interrupt_dispatcher->device_   = device;
    interrupt_dispatcher->maskable_ = maskable;
    *out_interrupt                  = dispatcher;
    *out_rights                     = kDefaultPciInterruptRights;
    return NO_ERROR;
}

void PciInterruptDispatcher::Close() {
    {
        AutoLock lock(&lock_);

        // If we no longer have a reference to to the underlying PCI device, we
        // are already closed and don't need to do anything else.
        if (!device_)
            return;

        // Start by unregistering our IRQ handler with the bus driver.
        __UNUSED status_t ret;
        ret = pcie_register_irq_handler(&device_->device()->common, irq_id_, NULL, NULL);
        DEBUG_ASSERT(ret == NO_ERROR);  // This should never fail.

        // Release our reference to our device in order to indicate that we are
        // now closed, then leave the main lock.
        device_ = nullptr;
    }

    // Force signal our event one last time to make certain that anyone who
    // might have been waiting on the IRQ has been released.
    event_signal(&event_, true);
}

status_t PciInterruptDispatcher::InterruptWait() {
    // Obtain the main lock and make sure that we are not currently closed.
    {
        status_t result;
        AutoLock lock(&lock_);
        if (!device_)
            return ERR_BAD_HANDLE;

        // Try to grab the wait_lock.  If we can't, it's because someone is already
        // waiting on the interrupt.  Right now, we only support a single waiter at
        // a time, so tell the user code that we are busy.
        result = mutex_acquire_timeout(&wait_lock_, 0);
        if (result != NO_ERROR) {
            DEBUG_ASSERT(result == ERR_TIMED_OUT);
            return ERR_BUSY;
        }

        // If IRQ masking is supported, unmask the IRQ.  If/when it fires, it
        // will signal our event and then disable itself.
        if (maskable_) {
            DEBUG_ASSERT(device_->device() && device_->claimed());
            result = pcie_unmask_irq(&device_->device()->common, irq_id_);
            if (result != NO_ERROR) {
                // Something went horribly wrong.  Be sure to release the wait
                // lock before we unwind our of the main lock and report our
                // shameful failure to the caller.
                mutex_release(&wait_lock_);
                return result;
            }
        }

        // Fall out of the main AutoLock scope, releasing the main lock in the
        // process.  NOTE: we are still holding the wait lock.
    }

    // Wait on our event while holding the wait lock.
    event_wait(&event_);

    // Release the wait lock, then obtain the main lock and check to see if we
    // were closed while waiting for the interrupt to fire.  NOTE: it is very
    // important to release the wait lock before attempting to acquire the main
    // lock.  Failure to do this can produce a lock ordering problem which will
    // lead to deadlock.
    mutex_release(&wait_lock_);
    {
        AutoLock lock(&lock_);
        return device_ ? NO_ERROR : ERR_CANCELLED;
    }
}
