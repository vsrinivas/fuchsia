// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/virtual_interrupt_dispatcher.h>

#include <dev/interrupt.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <kernel/auto_lock.h>
#include <lib/counters.h>
#include <platform.h>
#include <zircon/rights.h>

KCOUNTER(dispatcher_virtual_interrupt_create_count, "dispatcher.virtual_interrupt.create")
KCOUNTER(dispatcher_virtual_interrupt_destroy_count, "dispatcher.virtual_interrupt.destroy")

zx_status_t VirtualInterruptDispatcher::Create(KernelHandle<InterruptDispatcher>* handle,
                                               zx_rights_t* rights,
                                               uint32_t options) {

    if (options != ZX_INTERRUPT_VIRTUAL)
        return ZX_ERR_INVALID_ARGS;

    // Attempt to construct the dispatcher.
    fbl::AllocChecker ac;
    KernelHandle new_handle(fbl::AdoptRef(new (&ac) VirtualInterruptDispatcher()));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    new_handle.dispatcher()->set_flags(INTERRUPT_VIRTUAL);

    // Transfer control of the new dispatcher to the creator and we are done.
    *rights = default_rights();
    *handle = ktl::move(new_handle);

    return ZX_OK;
}

//void VirtualInterruptDispatcher::IrqHandler(void* ctx) { }

void VirtualInterruptDispatcher::MaskInterrupt() { }

void VirtualInterruptDispatcher::UnmaskInterrupt() { }

void VirtualInterruptDispatcher::UnregisterInterruptHandler() { }

VirtualInterruptDispatcher::VirtualInterruptDispatcher() {
    kcounter_add(dispatcher_virtual_interrupt_create_count, 1);
}

VirtualInterruptDispatcher::~VirtualInterruptDispatcher() {
    kcounter_add(dispatcher_virtual_interrupt_destroy_count, 1);
}
