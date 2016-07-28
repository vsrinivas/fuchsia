// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <dev/interrupt_event.h>

#include <assert.h>
#include <list.h>
#include <sys/types.h>
#include <kernel/thread.h>
#include <kernel/wait.h>
#include <dev/interrupt.h>
#include <stdlib.h>

#define INTERRUPT_EVENT_MAGIC (0x696e7472)  // "intr"

typedef struct interrupt_event_impl {
    int magic;
    struct list_node node;
    bool signalled;
    int woken_count;
    unsigned int vector;
    uint flags;
    wait_queue_t wait;
} interrupt_event_impl_t;

#define INTERRUPT_EVENT_INITIAL_VALUE(iei, initial, _vector, _flags) \
{ \
    .magic = INTERRUPT_EVENT_MAGIC, \
    .signalled = initial, \
    .woken_count = 0, \
    .vector = _vector, \
    .flags = _flags, \
    .wait = WAIT_QUEUE_INITIAL_VALUE((iei).wait), \
}

static struct list_node interrupt_event_list = LIST_INITIAL_VALUE(interrupt_event_list);

static spin_lock_t lock = SPIN_LOCK_INITIAL_VALUE;

static interrupt_event_impl_t *get_interrupt_event(unsigned int vector)
{
    DEBUG_ASSERT(spin_lock_held(&lock));

    interrupt_event_impl_t *iei;
    list_for_every_entry(&interrupt_event_list, iei, interrupt_event_impl_t, node) {
        if (iei->vector == vector) {
            return iei;
        }
    }
    return NULL;
}

static enum handler_return interrupt_event_int_handler(void *arg)
{
    interrupt_event_impl_t *iei = (interrupt_event_impl_t *)arg;

    THREAD_LOCK(state);

    mask_interrupt(iei->vector);

    // wait up threads waiting for this interrupt
    iei->woken_count = wait_queue_wake_all(&iei->wait, false, NO_ERROR);
    if (iei->woken_count <= 0) {
        // if no threads are woken up, mark the interrupt as signalled
        iei->signalled = true;
    }

    THREAD_UNLOCK(state);

    // reschedule if there are any threads waiting for the interrupt
    if (iei->woken_count > 0) {
        return INT_RESCHEDULE;
    } else {
        return INT_NO_RESCHEDULE;
    }
}

status_t interrupt_event_create(unsigned int vector, uint32_t flags, interrupt_event_t *ie)
{
    if (flags & INTERRUPT_EVENT_FLAG_REMAP_IRQ) vector = remap_interrupt(vector);

    if (!is_valid_interrupt(vector, flags)) return ERR_INVALID_ARGS;

    interrupt_event_impl_t *iei = calloc(1, sizeof(interrupt_event_impl_t));
    // an entry could already exist for this vector even if we failed to allocate, don't
    // error here

    spin_lock(&lock);

    // check if an entry already exists for this vector
    interrupt_event_impl_t *existing = get_interrupt_event(vector);
    if (existing == NULL) {

        if (iei) {
            // initialize the new entry and add to the list if there is no existing entry
            *iei = (interrupt_event_impl_t)INTERRUPT_EVENT_INITIAL_VALUE(*iei, false, vector, flags);
            list_add_tail(&interrupt_event_list, &iei->node);

            register_int_handler(vector, &interrupt_event_int_handler, iei);
            unmask_interrupt(vector);

            spin_unlock(&lock);
        } else {
            spin_unlock(&lock);
            // fail if we cannot allocate and there is no existing entry
            return ERR_NO_MEMORY;
        }

    } else {
        spin_unlock(&lock);

        // if an entry already exists, free the new entry and return the existing entry
        free(iei);
        iei = existing;
    }

    *ie = (interrupt_event_t)(iei);

    return NO_ERROR;
}

void interrupt_destroy(interrupt_event_t ie)
{
    interrupt_event_impl_t *iei = (interrupt_event_impl_t *)ie;

    DEBUG_ASSERT(iei->magic == INTERRUPT_EVENT_MAGIC);

    THREAD_LOCK(state);

    iei->magic = 0;
    iei->signalled = false;
    iei->flags = 0;
    wait_queue_destroy(&iei->wait, true);

    THREAD_UNLOCK(state);
}

status_t interrupt_event_wait(interrupt_event_t ie)
{
    status_t ret = NO_ERROR;
    interrupt_event_impl_t *iei = (interrupt_event_impl_t *)ie;

    DEBUG_ASSERT(iei->magic == INTERRUPT_EVENT_MAGIC);

    THREAD_LOCK(state);

    if (iei->signalled) {
        // has pending interrupt, fall through
        iei->signalled = false;
    } else {
        ret = wait_queue_block(&iei->wait, INFINITE_TIME);
    }

    THREAD_UNLOCK(state);

    return ret;
}

void interrupt_event_complete(interrupt_event_t ie)
{
    interrupt_event_impl_t *iei = (interrupt_event_impl_t *)ie;

    DEBUG_ASSERT(iei->magic == INTERRUPT_EVENT_MAGIC);

    THREAD_LOCK(state);

    // TODO(yky): maybe we need a token
    if (iei->woken_count > 0)
        iei->woken_count--;

    if (iei->woken_count == 0)
        unmask_interrupt(iei->vector);

    THREAD_UNLOCK(state);
}

