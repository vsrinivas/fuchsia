// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>
#include <list.h>
#include <sys/types.h>

__BEGIN_CDECLS

#define DPC_THREAD_PRIORITY HIGH_PRIORITY

struct dpc;
typedef void (*dpc_func_t)(struct dpc *);

typedef struct dpc {
    struct list_node node;

    dpc_func_t func;
    void *arg;
} dpc_t;

#define DPC_INITIAL_VALUE \
{ \
    .node = LIST_INITIAL_CLEARED_VALUE, \
    .func = 0, \
    .arg = 0, \
}

/* queue an already filled out dpc, optionally reschedule immediately to run the dpc thread */
/* the deferred procedure runs in a dedicated thread that runs at DPC_THREAD_PRIORITY */
mx_status_t dpc_queue(dpc_t *dpc, bool reschedule);

/* queue a dpc, but must be holding the thread lock */
/* does not force a reschedule */
mx_status_t dpc_queue_thread_locked(dpc_t *dpc);

/* Cancels a previously queued dpc. Returns true if the the dpc was canceled */
/* before it was scheduled to run. */
bool dpc_cancel(dpc_t *dpc);

__END_CDECLS
