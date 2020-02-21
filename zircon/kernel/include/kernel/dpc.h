// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_DPC_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_DPC_H_

#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include <kernel/thread.h>

__BEGIN_CDECLS

// Deferred Procedure Calls - queue callback to invoke on the current CPU in thread context.
// DPCs are executed with interrupts enabled; DPCs do not ever migrate CPUs while executing.
// A DPC may not execute on the original current CPU if it is hotunplugged/offlined.
// DPCs may block, though this may starve other queued work.

#define DPC_THREAD_PRIORITY HIGH_PRIORITY

struct dpc;
typedef void (*dpc_func_t)(struct dpc*);

typedef struct dpc {
  struct list_node node;

  dpc_func_t func;
  void* arg;
} dpc_t;

#define DPC_INITIAL_VALUE \
  { .node = LIST_INITIAL_CLEARED_VALUE, .func = 0, .arg = 0, }

// initializes dpc for the current cpu
void dpc_init_for_cpu(void);

// queue an already filled out dpc, optionally reschedule immediately to run the dpc thread.
// the deferred procedure runs in a dedicated thread that runs at DPC_THREAD_PRIORITY
// dpc_queue will not block; it may wait briefly for a spinlock.
zx_status_t dpc_queue(dpc_t* dpc, bool reschedule);

// queue a dpc, but must be holding the thread lock
// does not force a reschedule
// dpc_queue_thread_locked will not block; it may wait briefly for a spinlock.
// |dpc| may be deallocated once the function starts executing.
// |dpc.func| may requeue the DPC if needed.
zx_status_t dpc_queue_thread_locked(dpc_t* dpc) TA_REQ(thread_lock);

// Begins the DPC shutdown process for |cpu|.
//
// Shutting down a DPC queue is a two-phase process.  This is the first phase.  See
// |dpc_shutdown_transition_off_cpu| for the second phase.
//
// This function:
// - stops servicing the queue
// - waits for any in-progress DPC to complete
// - ensures no queued DPCs will begin executing
// - joins the DPC thread
//
// Upon completion, |cpu| may have unexecuted DPCs and |dpc_queue| will continue to queue new DPCs.
//
// Once |cpu| is no longer executing tasks, finish the shutdown process by calling
// |dpc_shutdown_transition_off_cpu|.
void dpc_shutdown(uint cpu);

// Moves queued DPCs from |cpu| to the caller's CPU.
//
// This is the second phase of DPC shutdown.  See |dpc_shutdown|.
//
// Should only be called after |cpu| has stopped executing tasks.
void dpc_shutdown_transition_off_cpu(uint cpu);

__END_CDECLS

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_DPC_H_
