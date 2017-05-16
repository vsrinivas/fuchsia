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
#include <kernel/auto_lock.h>
#include <kernel/mp.h>
#include <kernel/thread.h>
#include <malloc.h>
#include <string.h>

#include <bitmap/rle-bitmap.h>
#include <mxalloc/new.h>
#include <mxtl/unique_ptr.h>

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

    // This is overkill, but it's much simpler to reason about
    x86_reset_tss_io_bitmap();
    x86_set_tss_io_bitmap(*static_cast<bitmap::RleBitmap*>(as->io_bitmap));

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

    mxtl::unique_ptr<bitmap::RleBitmap> optimistic_bitmap;
    if (!as->io_bitmap) {
        // Optimistically allocate a bitmap structure if we don't have one, and
        // we'll see if we actually need this allocation later.  In the common
        // case, when we make the allocation we will use it.
        AllocChecker ac;
        optimistic_bitmap.reset(new (&ac) bitmap::RleBitmap());
        if (!ac.check()) {
            return ERR_NO_MEMORY;
        }
    }

    // Create a free-list in case any of our bitmap operations need to free any
    // nodes.
    bitmap::RleBitmap::FreeList bitmap_freelist;

    // Optimistically allocate an element for the bitmap, in case we need one.
    {
        AllocChecker ac;
        bitmap_freelist.push_back(mxtl::unique_ptr<bitmap::RleBitmapElement>(new (&ac) bitmap::RleBitmapElement()));
        if (!ac.check()) {
            return ERR_NO_MEMORY;
        }
    }

    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);

    status_t status = NO_ERROR;
    do {
        AutoSpinLock guard(as->io_bitmap_lock);

        if (!as->io_bitmap) {
            as->io_bitmap = optimistic_bitmap.release();
        }
        auto bitmap = static_cast<bitmap::RleBitmap*>(as->io_bitmap);
        DEBUG_ASSERT(bitmap);

        status = enable ?
                bitmap->SetNoAlloc(port, port + len, &bitmap_freelist) :
                bitmap->ClearNoAlloc(port, port + len, &bitmap_freelist);
        if (status != NO_ERROR) {
            break;
        }

        // Set the io bitmap in the tss (the tss IO bitmap has reversed polarity)
        tss_t* tss = &x86_get_percpu()->default_tss;
        if (enable) {
            bitmap_clear(reinterpret_cast<unsigned long*>(tss->tss_bitmap), port, len);
        } else {
            bitmap_set(reinterpret_cast<unsigned long*>(tss->tss_bitmap), port, len);
        }
    } while (0);

    // Let all other CPUs know about the update
    if (status == NO_ERROR) {
        struct ioport_update_context task_context = {.aspace = as};
        mp_sync_exec(MP_CPU_ALL_BUT_LOCAL, ioport_update_task, &task_context);
    }

    arch_interrupt_restore(state, 0);
    return status;
}

void x86_set_tss_io_bitmap(const bitmap::RleBitmap& bitmap)
{
    DEBUG_ASSERT(arch_ints_disabled());
    tss_t *tss = &x86_get_percpu()->default_tss;

    auto tss_bitmap = reinterpret_cast<unsigned long*>(tss->tss_bitmap);
    for (const auto& extent : bitmap) {
        DEBUG_ASSERT(extent.bitoff + extent.bitlen <= IO_BITMAP_BITS);
        bitmap_clear(tss_bitmap, static_cast<int>(extent.bitoff), static_cast<int>(extent.bitlen));
    }
}

void x86_reset_tss_io_bitmap(void) {
    DEBUG_ASSERT(arch_ints_disabled());
    tss_t *tss = &x86_get_percpu()->default_tss;
    auto tss_bitmap = reinterpret_cast<unsigned long*>(tss->tss_bitmap);

    bitmap_set(tss_bitmap, 0, IO_BITMAP_BITS);
}

void x86_clear_tss_io_bitmap(const bitmap::RleBitmap& bitmap)
{
    DEBUG_ASSERT(arch_ints_disabled());
    tss_t *tss = &x86_get_percpu()->default_tss;

    auto tss_bitmap = reinterpret_cast<unsigned long*>(tss->tss_bitmap);
    for (const auto& extent : bitmap) {
        DEBUG_ASSERT(extent.bitoff + extent.bitlen <= IO_BITMAP_BITS);
        bitmap_set(tss_bitmap, static_cast<int>(extent.bitoff), static_cast<int>(extent.bitlen));
    }
}
