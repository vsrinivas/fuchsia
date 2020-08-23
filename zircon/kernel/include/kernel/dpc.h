// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_DPC_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_DPC_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

#include <fbl/intrusive_double_list.h>
#include <kernel/event.h>
#include <kernel/thread.h>
#include <kernel/thread_lock.h>

// Deferred Procedure Calls - queue callback to invoke on the current cpu in thread context.
// Dpcs are executed with interrupts enabled, and do not ever migrate cpus while executing.
// A Dpc may not execute on the original current cpu if it is hotunplugged/offlined.
// Dpcs may block, though this may starve other queued work.

class Dpc : public fbl::DoublyLinkedListable<Dpc*, fbl::NodeOptions::AllowCopyMove> {
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
  // scheduler to execute next may or may not be the Dpc worker thread.
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
  friend class DpcQueue;

  // The DpcQueue this Dpc gets enqueued onto is the only thing to actually Invoke this Dpc,
  // on its worker thread.
  void Invoke();

  Func* func_;
  void* arg_;
};

// Each cpu maintains a DpcQueue, in its percpu structure.
class DpcQueue {
 public:
  // Initializes this DpcQueue for the current cpu.
  void InitForCurrentCpu();

  // Begins the Dpc shutdown process for the owning cpu.
  //
  // Shutting down a Dpc queue is a two-phase process.  This is the first phase.  See
  // |TransitionOffCpu| for the second phase.
  //
  // This method:
  // - tells the owning cpu's Dpc thread to stop servicing its queue then
  // - waits, up to |deadline|, for it to finish any in-progress DPC and join
  //
  // Because this method blocks until the Dpc thread has terminated, it is critical that the caller
  // not hold any locks that might be needed by any previously queued DPCs.  Otheriwse, deadlock may
  // occur.
  //
  // Upon successful completion, this DpcQueue may contain unexecuted Dpcs and new ones
  // may be added by |Queue|.  However, they will not execute (on any cpu) until
  // |TransitionOffCpu| is called.
  //
  // Once |Shutdown| has completed successfully, finish the shutdown process by calling
  // |TransitionOffCpu| on some cpu other than the owning cpu.
  //
  // If |Shutdown| fails, this DpcQueue is left in an undefined state and
  // |TransitionOffCpu| must not be called.
  zx_status_t Shutdown(zx_time_t deadline);

  // Moves queued Dpcs from |source| to this DpcQueue.
  //
  // This is the second phase of Dpc shutdown.  See |Shutdown|.
  //
  // This must only be called after |Shutdown| has completed successfully.
  //
  // This must only be called on the current cpu.
  void TransitionOffCpu(DpcQueue& source);

  // These are called by Dpc::Queue and Dpc::QueueThreadLocked.
  void Enqueue(Dpc* dpc);
  void Signal(bool reschedule) TA_EXCL(thread_lock);
  void SignalLocked() TA_REQ(thread_lock);

 private:
  static int WorkerThread(void* unused);
  int Work();

  // The cpu that owns this DpcQueue.
  cpu_num_t cpu_ = INVALID_CPU;

  // Whether the DpcQueue has been initialized for the owning cpu.
  bool initialized_ = false;

  // Request the thread_ to stop by setting to true.
  //
  // This guarded by the static global dpc_lock.
  bool stop_ = false;

  // This guarded by the static global dpc_lock.
  fbl::DoublyLinkedList<Dpc*> list_;

  Event event_;

  // Each cpu maintains a dedicated thread for processing Dpcs.
  Thread* thread_ = nullptr;
};

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_DPC_H_
