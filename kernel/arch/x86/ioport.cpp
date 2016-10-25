// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/x86.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/ioport.h>
#include <arch/x86/mp.h>
#include <assert.h>
#include <bits.h>
#include <err.h>
#include <kernel/mp.h>
#include <kernel/thread.h>
#include <malloc.h>
#include <string.h>

#include <mxtl/unique_ptr.h>
#include <new.h>

/* Task used for updating IO permissions on each CPU */
struct ioport_update_context {
    // aspace that we're trying to update
    arch_aspace_t* aspace;
};
static void ioport_update_task(void* raw_context) {
    DEBUG_ASSERT(arch_ints_disabled());
    struct ioport_update_context* context =
        (struct ioport_update_context*)raw_context;

    thread_t* t = get_current_thread();
    if (!t->aspace) {
        return;
    }

    struct arch_aspace* as = vmm_get_arch_aspace(t->aspace);
    if (as != context->aspace) {
        return;
    }

    spin_lock(&as->io_bitmap_lock);
    x86_set_tss_io_bitmap(static_cast<uint8_t*>(as->io_bitmap_ptr));
    spin_unlock(&as->io_bitmap_lock);
}

int x86_set_io_bitmap(uint32_t port, uint32_t len, bool enable) {
    DEBUG_ASSERT(!arch_ints_disabled());

    if ((port + len < port) || (port + len > IO_BITMAP_BITS))
        return ERR_INVALID_ARGS;

    thread_t* t = get_current_thread();
    DEBUG_ASSERT(t->aspace);
    if (!t->aspace) {
        return ERR_INVALID_ARGS;
    }

    struct arch_aspace* as = vmm_get_arch_aspace(t->aspace);

    // Optimistically allocate the bitmap pointer if it doesn't exist.  Once
    // we're in the spinlock, we'll see if we actually need this allocation or
    // not.  In the common case, when we make the allocation we will use it.
    mxtl::unique_ptr<unsigned long> optimistic_alloc;
    if (!as->io_bitmap_ptr) {
        AllocChecker ac;
        optimistic_alloc.reset(new (&ac) unsigned long[IO_BITMAP_LONGS]());
        if (!ac.check()) {
            return ERR_NO_MEMORY;
        }
    }

    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);
    spin_lock(&as->io_bitmap_lock);

    // Initialize the io bitmap if this is the first call for this process
    if (as->io_bitmap_ptr == NULL) {
        DEBUG_ASSERT(optimistic_alloc);
        if (!optimistic_alloc) {
            spin_unlock(&as->io_bitmap_lock);
            arch_interrupt_restore(state, 0);
            return ERR_NO_MEMORY;
        }

        as->io_bitmap_ptr = optimistic_alloc.release();
    }

    // Set the io bitmap in the thread structure and the tss
    tss_t* tss = &x86_get_percpu()->default_tss;

    if (enable) {
        bitmap_clear(static_cast<unsigned long*>(as->io_bitmap_ptr), port, len);
        bitmap_clear(reinterpret_cast<unsigned long*>(tss->tss_bitmap), port, len);
    } else {
        bitmap_set(static_cast<unsigned long*>(as->io_bitmap_ptr), port, len);
        bitmap_set(reinterpret_cast<unsigned long*>(tss->tss_bitmap), port, len);
    }

    spin_unlock(&as->io_bitmap_lock);

    // Let all other CPUs know about the update
    struct ioport_update_context task_context = {.aspace = as};
    mp_sync_exec(MP_CPU_ALL_BUT_LOCAL, ioport_update_task, &task_context);

    arch_interrupt_restore(state, 0);

    return NO_ERROR;
}
