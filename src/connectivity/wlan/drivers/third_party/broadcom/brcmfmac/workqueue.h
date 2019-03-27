/*
 * Copyright (c) 2018 The Fuchsia Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef GARNET_DRIVERS_WLAN_THIRD_PARTY_BROADCOM_BRCMFMAC_WORKQUEUE_H_
#define GARNET_DRIVERS_WLAN_THIRD_PARTY_BROADCOM_BRCMFMAC_WORKQUEUE_H_

#include <zircon/listnode.h>
#include <zircon/syscalls.h>

// This is a PARTIAL implementation of Linux work queues.
//
// Linux workqueues pay attention to which CPU work is scheduled on. This implementation does
// not.
//
// Every work queue, including the default one accessed through workqueue_schedule_default(), is
// single-threaded. In Linux, they're per-CPU by default, so several works may run in parallel.
// This may cause slowness.

#define WORKQUEUE_NAME_MAXLEN 64

struct workqueue_struct;

// handler: The callback function.
// signaler: If not ZX_HANDLE_INVALID, will be signaled WORKQUEUE_SIGNAL on completion of work.
// workqueue: The work queue currently queued or executing on.
// item: If work is queued, item is the link to the work list.
struct work_struct {
    void (*handler)(struct work_struct *);
    zx_handle_t signaler;
    struct workqueue_struct* workqueue;
    list_node_t item;
};

void workqueue_init_work(struct work_struct* work, void (*handler)(struct work_struct* work));

// Creates a single-threaded workqueue, which must eventually be given to workqueue_destroy for
// disposal.
struct workqueue_struct* workqueue_create(const char* name);

// Waits for currently scheduled work to finish, then tears down the queue. It is illegal to
// schedule new work after calling workqueue_destroy, including current work scheduling new work.
void workqueue_destroy(struct workqueue_struct* queue);

// Waits for any work on workqueue at time of call to complete. Jobs scheduled after flush
// starts, including work scheduled by pre-flush work, will not be waited for.
void workqueue_flush(struct workqueue_struct* workqueue);
void workqueue_flush_default(void);

// Queues work on the given work queue. Work will be executed one at a time in order queued (FIFO).
void workqueue_schedule(struct workqueue_struct* queue, struct work_struct* work);

// Queues work on the global default work queue, creating the work queue if necessary.
void workqueue_schedule_default(struct work_struct* work);

// If work isn't started, deletes it. If it was started, waits for it to finish. Thus, this
// may block. Either way, the work is guaranteed not to be running after workqueue_cancel_work
// returns.
void workqueue_cancel_work(struct work_struct* work);

#endif // GARNET_DRIVERS_WLAN_THIRD_PARTY_BROADCOM_BRCMFMAC_WORKQUEUE_H_
