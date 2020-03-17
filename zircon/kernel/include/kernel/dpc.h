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

#include <fbl/intrusive_double_list.h>
#include <kernel/thread.h>

// Deferred Procedure Calls - queue callback to invoke on the current CPU in thread context.
// DPCs are executed with interrupts enabled; DPCs do not ever migrate CPUs while executing.
// A DPC may not execute on the original current CPU if it is hotunplugged/offlined.
// DPCs may block, though this may starve other queued work.

class Dpc : public fbl::DoublyLinkedListable<Dpc*> {
 public:
  using Func = void(Dpc*);

  explicit Dpc(Func* func = nullptr, void* arg = nullptr) : func_(func), arg_(arg) {}

  template <class ArgType>
  ArgType* arg() {
    return static_cast<ArgType*>(arg_);
  }

  // Queue an already filled out dpc, optionally reschedule immediately to run the dpc thread.
  // the deferred procedure runs in a dedicated thread that runs at DPC_THREAD_PRIORITY
  // |Queue| will not block; it may wait briefly for a spinlock.
  //
  // Returns ZX_ERR_ALREADY_EXISTS if |this| is already queued.
  zx_status_t Queue(bool reschedule);

  // Queue a dpc, but must be holding the thread lock.
  // This does not force a reschedule.
  // QueueThreadLocked will not block; it may wait briefly for a spinlock.
  // |this| may be deallocated once the function starts executing.
  // |func_| may requeue the DPC if needed.
  //
  // Returns ZX_ERR_ALREADY_EXISTS if |this| is already queued.
  zx_status_t QueueThreadLocked() TA_REQ(thread_lock);

  // Initializes the DPC subsystem for the current cpu.
  static void InitForCpu();

  // Begins the DPC shutdown process for |cpu|.
  //
  // Shutting down a DPC queue is a two-phase process.  This is the first phase.  See
  // |ShutdownTransitionOffCpu| for the second phase.
  //
  // This function:
  // - tells |cpu|'s DPC thread to stop servicing its queue then
  // - waits, up to |deadline|, for it to finish any in-progress DPC and join
  //
  // Upon successful completion, |cpu|'s DPC queue may contain unexecuted DPCs and new ones may be
  // added by |Queue|.  However, they will not execute (on any CPU) until |ShutdownTransitionOffCpu|
  // is called.
  //
  // Once |Shutdown| for |cpu| has completed successfully, finish the shutdown process by calling
  // |ShutdownTransitionOffCpu| on some CPU other than |cpu|.
  //
  // If |Shutdown| fails, the DPC system for |cpu| is left in an undefined state and
  // |ShutdownTransitionOffCpu| must not be called.
  static zx_status_t Shutdown(uint cpu, zx_time_t deadline);

  // Moves queued DPCs from |cpu| to the caller's CPU.
  //
  // This is the second phase of DPC shutdown.  See |Shutdown|.
  //
  // Should only be called after |Shutdown| for |cpu| has completed successfully.
  static void ShutdownTransitionOffCpu(uint cpu);

 private:
  static int WorkerThread(void* arg);

  Func* func_;
  void* arg_;
};

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_DPC_H_
