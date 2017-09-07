// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/interrupt_event_dispatcher.h>

#include <kernel/auto_lock.h>
#include <dev/interrupt.h>
#include <magenta/rights.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#include <err.h>

// Static storage
fbl::Mutex InterruptEventDispatcher::vectors_lock_;
InterruptEventDispatcher::VectorCollection InterruptEventDispatcher::vectors_;

// static
mx_status_t InterruptEventDispatcher::Create(uint32_t vector,
                                             uint32_t flags,
                                             fbl::RefPtr<Dispatcher>* dispatcher,
                                             mx_rights_t* rights) {
    // Remap the vector if we have been asked to do so.
    if (flags & MX_FLAG_REMAP_IRQ)
        vector = remap_interrupt(vector);

    // If this is not a valid interrupt vector, fail.
    if (!is_valid_interrupt(vector, 0))
        return MX_ERR_INVALID_ARGS;

    // Attempt to construct the dispatcher.
    fbl::AllocChecker ac;
    InterruptEventDispatcher* disp = new (&ac) InterruptEventDispatcher(vector);
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    // Hold a ref while we check to see if someone else owns this vector or not.
    // If things go wrong, this ref will be released and the IED will get
    // cleaned up automatically.
    auto disp_ref = fbl::AdoptRef<Dispatcher>(disp);

    // Attempt to add ourselves to the vector collection.
    {
        fbl::AutoLock lock(&vectors_lock_);
        if (!vectors_.insert_or_find(disp))
            return MX_ERR_ALREADY_EXISTS;
    }

    // Looks like things went well.  Register our callback and unmask our
    // interrupt.
    register_int_handler(vector, IrqHandler, disp);
    unmask_interrupt(vector);

    // Transfer control of the new dispatcher to the creator and we are done.
    *rights     = MX_DEFAULT_INTERRUPT_RIGHTS;
    *dispatcher = fbl::move(disp_ref);

    return MX_OK;
}

InterruptEventDispatcher::~InterruptEventDispatcher() {
    // If we were successfully instantiated, then we must exist in the vector
    // collection.  Unconditionally mask our vector, clear out our handler and
    // remove ourselves from the collection bookkeeping (allowing others to
    // claim the vector if they desire).
    if (wavl_node_state_.InContainer()) {
        mask_interrupt(vector_);
        register_int_handler(vector_, nullptr, nullptr);
        {
            fbl::AutoLock lock(&vectors_lock_);
            vectors_.erase(*this);
        }
    }
}

mx_status_t InterruptEventDispatcher::InterruptComplete() {
    canary_.Assert();

    unsignal();
    unmask_interrupt(vector_);
    return MX_OK;
}

mx_status_t InterruptEventDispatcher::UserSignal() {
    canary_.Assert();

    mask_interrupt(vector_);
    signal(true);
    return MX_OK;
}

enum handler_return InterruptEventDispatcher::IrqHandler(void* ctx) {
    InterruptEventDispatcher* thiz = reinterpret_cast<InterruptEventDispatcher*>(ctx);

    // TODO(johngro): make sure that this is safe to do from an IRQ.
    mask_interrupt(thiz->vector_);

    if (thiz->signal() > 0) {
        return INT_RESCHEDULE;
    } else {
        return INT_NO_RESCHEDULE;
    }
}
