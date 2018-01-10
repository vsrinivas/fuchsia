// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#pragma once

#include <list.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

#define DPC_THREAD_PRIORITY HIGH_PRIORITY

struct dpc;
typedef void (*dpc_func_t)(struct dpc*);

typedef struct dpc {
    struct list_node node;

    dpc_func_t func;
    void* arg;
} dpc_t;

#define DPC_INITIAL_VALUE                   \
    {                                       \
        .node = LIST_INITIAL_CLEARED_VALUE, \
        .func = 0,                          \
        .arg = 0,                           \
    }

/* initializes dpc for the current cpu */
void dpc_init_for_cpu(void);

/* queue an already filled out dpc, optionally reschedule immediately to run the dpc thread */
/* the deferred procedure runs in a dedicated thread that runs at DPC_THREAD_PRIORITY */
zx_status_t dpc_queue(dpc_t* dpc, bool reschedule);

/* queue a dpc, but must be holding the thread lock */
/* does not force a reschedule */
zx_status_t dpc_queue_thread_locked(dpc_t* dpc);

/* Moves pending dpcs for the given CPU to the caller's CPU */
void dpc_transition_off_cpu(uint cpu);

__END_CDECLS
