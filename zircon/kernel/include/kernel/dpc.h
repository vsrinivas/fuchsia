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

struct DpcSystem;

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

  // Queue this object and signal the worker thread to execute it.
  //
  // |Queue| will not block, but it may wait briefly for a spinlock.
  //
  // If |reschedule| is true, ask the scheduler to reschedule immediately.  The thread chosen by the
  // scheduler to execute next may or may not be the DPC worker thread.
  //
  // |Queue| may return before or after the Dpc has executed.  It is the caller's responsibilty to
  // ensure that a queued Dpc object is not destroyed prior to its execution.
  //
  // Returns ZX_ERR_ALREADY_EXISTS if |this| is already queued.
  zx_status_t Queue(bool reschedule);

  // Queue this object and signal the worker thread to execute it.
  //
  // This method is similar to |Queue| with |reschedule| equal to false, except that it must be
  // called while holding the ThreadLock.
  //
  // |QueueThreadLocked| may return before or after the Dpc has executed.  It is the caller's
  // responsibilty to ensure that a queued Dpc object is not destroyed prior to its execution.
  //
  // Returns ZX_ERR_ALREADY_EXISTS if |this| is already queued.
  zx_status_t QueueThreadLocked() TA_REQ(thread_lock);

 private:
  // The DpcSystem, or more specifically, its worker threads, are the only thing to actually call
  // the functions queued onto Dpcs.
  friend struct DpcSystem;
  void Invoke();

  Func* func_;
  void* arg_;
};

struct DpcSystem {
  // Initializes the DPC subsystem for the current cpu.
  static void InitForCpu();

  // Begins the DPC shutdown process for |cpu|.
  //
  // Shutting down a DPC queue is a two-phase process.  This is the first phase.  See
  // |ShutdownTransitionOffCpu| for the second phase.
  //
  // This method:
  // - tells |cpu|'s DPC thread to stop servicing its queue then
  // - waits, up to |deadline|, for it to finish any in-progress DPC and join
  //
  // Because this method blocks until the DPC thread has terminated, it is critical that the caller
  // not hold any locks that might be needed by any previously queued DPCs.  Otheriwse, deadlock may
  // occur.
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
};

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_DPC_H_
