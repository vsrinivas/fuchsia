// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/interrupt_event_dispatcher.h>

#include <kernel/auto_lock.h>
#include <dev/interrupt.h>
#include <zircon/rights.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <platform.h>

// static
zx_status_t InterruptEventDispatcher::Create(fbl::RefPtr<Dispatcher>* dispatcher,
                                             zx_rights_t* rights) {
    // Attempt to construct the dispatcher.
    fbl::AllocChecker ac;
    InterruptEventDispatcher* disp = new (&ac) InterruptEventDispatcher();
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    // Hold a ref while we check to see if someone else owns this vector or not.
    // If things go wrong, this ref will be released and the IED will get
    // cleaned up automatically.
    auto disp_ref = fbl::AdoptRef<Dispatcher>(disp);

    fbl::AutoLock lock(disp->get_lock());

    // prebind ZX_INTERRUPT_SLOT_USER
    zx_status_t status = disp->AddSlotLocked(ZX_INTERRUPT_SLOT_USER, 0, INTERRUPT_VIRTUAL);
    if (status != ZX_OK)
        return status;

    // Transfer control of the new dispatcher to the creator and we are done.
    *rights     = ZX_DEFAULT_INTERRUPT_RIGHTS;
    *dispatcher = fbl::move(disp_ref);

    return ZX_OK;
}

zx_status_t InterruptEventDispatcher::Bind(uint32_t slot, uint32_t vector, uint32_t options) {
    canary_.Assert();

    if (slot > ZX_INTERRUPT_MAX_SLOTS)
        return ZX_ERR_INVALID_ARGS;

    bool is_virtual = !!(options & ZX_INTERRUPT_VIRTUAL);
    uint32_t interrupt_flags = 0;

    if (is_virtual) {
        if (options != ZX_INTERRUPT_VIRTUAL) {
            return ZX_ERR_INVALID_ARGS;
        }
        interrupt_flags = INTERRUPT_VIRTUAL;
    } else {
        if (options & ~(ZX_INTERRUPT_REMAP_IRQ | ZX_INTERRUPT_MODE_MASK))
            return ZX_ERR_INVALID_ARGS;

        // Remap the vector if we have been asked to do so.
        if (options & ZX_INTERRUPT_REMAP_IRQ)
            vector = remap_interrupt(vector);

        if (!is_valid_interrupt(vector, 0))
            return ZX_ERR_INVALID_ARGS;

        bool default_mode = false;
        enum interrupt_trigger_mode tm = IRQ_TRIGGER_MODE_EDGE;
        enum interrupt_polarity pol = IRQ_POLARITY_ACTIVE_LOW;
        switch (options & ZX_INTERRUPT_MODE_MASK) {
            case ZX_INTERRUPT_MODE_DEFAULT:
                default_mode = true;
                break;
            case ZX_INTERRUPT_MODE_EDGE_LOW:
                tm = IRQ_TRIGGER_MODE_EDGE;
                pol = IRQ_POLARITY_ACTIVE_LOW;
                break;
            case ZX_INTERRUPT_MODE_EDGE_HIGH:
                tm = IRQ_TRIGGER_MODE_EDGE;
                pol = IRQ_POLARITY_ACTIVE_HIGH;
                break;
            case ZX_INTERRUPT_MODE_LEVEL_LOW:
                tm = IRQ_TRIGGER_MODE_LEVEL;
                pol = IRQ_POLARITY_ACTIVE_LOW;
                interrupt_flags = INTERRUPT_UNMASK_PREWAIT | INTERRUPT_MASK_POSTWAIT;
                break;
            case ZX_INTERRUPT_MODE_LEVEL_HIGH:
                tm = IRQ_TRIGGER_MODE_LEVEL;
                pol = IRQ_POLARITY_ACTIVE_HIGH;
                interrupt_flags = INTERRUPT_UNMASK_PREWAIT | INTERRUPT_MASK_POSTWAIT;
                break;
            default:
                return ZX_ERR_INVALID_ARGS;
        }

        if (!default_mode) {
            zx_status_t status = configure_interrupt(vector, tm, pol);
            if (status != ZX_OK)
                return status;
        }
    }

    fbl::AutoLock lock(get_lock());

    zx_status_t status = AddSlotLocked(slot, vector, interrupt_flags);
    if (status != ZX_OK)
        return status;

    if (!is_virtual)
        unmask_interrupt(vector);

    return ZX_OK;
}

void InterruptEventDispatcher::IrqHandler(void* ctx) {
    Interrupt* interrupt = reinterpret_cast<Interrupt*>(ctx);

    // only record timestamp if this is the first IRQ since we started waiting
    zx_time_t zero_timestamp = 0;
    atomic_cmpxchg_u64(&interrupt->timestamp, &zero_timestamp, current_time());

    InterruptEventDispatcher* thiz
            = reinterpret_cast<InterruptEventDispatcher *>(interrupt->dispatcher);

    if (interrupt->flags & INTERRUPT_MASK_POSTWAIT)
        mask_interrupt(interrupt->vector);

    thiz->Signal(SIGNAL_MASK(interrupt->slot), true);
}

void InterruptEventDispatcher::MaskInterrupt(uint32_t vector) {
    mask_interrupt(vector);
}

void InterruptEventDispatcher::UnmaskInterrupt(uint32_t vector) {
    unmask_interrupt(vector);
}

zx_status_t InterruptEventDispatcher::RegisterInterruptHandler(uint32_t vector, void* data) {
    return register_int_handler(vector, IrqHandler, data);
}
void InterruptEventDispatcher::UnregisterInterruptHandler(uint32_t vector) {
    register_int_handler(vector, nullptr, nullptr);
}
