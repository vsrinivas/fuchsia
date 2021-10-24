// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_BACKTRACE_INCLUDE_LIB_BACKTRACE_CPU_CONTEXT_EXCHANGE_H_
#define ZIRCON_KERNEL_LIB_BACKTRACE_INCLUDE_LIB_BACKTRACE_CPU_CONTEXT_EXCHANGE_H_

#include <lib/affine/ratio.h>
#include <lib/backtrace.h>
#include <zircon/types.h>

#include <arch/regs.h>
#include <kernel/cpu.h>
#include <kernel/loop_limiter.h>
#include <kernel/thread.h>
#include <ktl/atomic.h>

// CpuContext contains the execution state of a CPU.
struct CpuContext {
  iframe_t frame{};
  Backtrace backtrace;
};

// CpuContextExchange is a place for a CPU to synchronously receive the context
// of another CPU.  CpuContextExchange is thread-safe and intended for
// concurrent use.
//
// Example usage:
//
//   // Sends an IPI to |target|.
//   void SendIpi(cput_num_t target);
//   CpuContextExchange exchange(SendIpi);
//
//   ...
//
//   // On CPU-1 with interrupts disabled...
//   CpuContextExchange context;
//   zx_status_t status = exchange.RequestContext(2, ZX_MSEC(10), context);
//   if (status == ZX_OK) {
//     Print(&context);
//   }
//
//   ...
//
//   // On CPU-2 with interrupts disabled...
//   void NmiHandler(iframe_t* frame) {
//     exchange.HandleRequest(frame->rbp, *frame);
//   }
//
// The template parameter |NotifyFn| is functor that accepts one cpu_num_t
// argument.  It will be called by |RequestContext| with interrupts disabled and
// be passed |target_cpu|.  When called it should *somehow* notify |target_cpu|
// that another CPU has requested its context.  The |target_cpu| should then
// call |HandleRequest|.
template <typename NotifyFn>
class CpuContextExchange {
 public:
  explicit CpuContextExchange(NotifyFn notify_fn) : notify_fn_(ktl::move(notify_fn)) {}

  // Synchronously request |target_cpu| to fill in |context|.  Spins until
  // |target_cpu| handles the request or |timeout| has elapsed.
  //
  // All requests for a given exchange instance are serialized so if the target
  // does not respond, the exchange will remain "tied up" indefinitely.  When
  // this happens, subsequent requests will spin for |timeout| before failing
  // with ZX_ERR_TIMED_OUT.
  //
  // Must be called with interrupts disabled.
  zx_status_t RequestContext(cpu_num_t target_cpu, zx_duration_t timeout, CpuContext& context);

  // Synchronously reply to a request.  This method is a no-op if there is no
  // active request for this CPU's context.
  //
  // Safe for use in interrupt context.
  //
  // Must be called with interrupts disabled.
  void HandleRequest(vaddr_t fp, const iframe_t& frame);

  // No copy.  No move.
  CpuContextExchange(const CpuContextExchange&) = delete;
  CpuContextExchange& operator=(const CpuContextExchange&) = delete;
  CpuContextExchange(CpuContextExchange&&) = delete;
  CpuContextExchange& operator=(CpuContextExchange&&) = delete;

 private:
  NotifyFn notify_fn_;

  // Acts like a spinlock and must be acquired by |RequestContext| prior to
  // modifying |target_cpu_|.  When held, this lock contains the cpu_num_t of
  // the holder.  When available it contains |INVALID_CPU|.
  ktl::atomic<cpu_num_t> requesting_cpu_{INVALID_CPU};

  // Indicates the CPU that should handle the request.  May only be cleared by
  // |target_cpu_|.
  ktl::atomic<cpu_num_t> target_cpu_{INVALID_CPU};

  // May only be written by |target_cpu_|.
  CpuContext storage_{};
};

template <typename NotifyFn>
zx_status_t CpuContextExchange<NotifyFn>::RequestContext(cpu_num_t target_cpu,
                                                         zx_duration_t timeout,
                                                         CpuContext& context) {
  DEBUG_ASSERT(arch_ints_disabled());

  // Use a LoopLimiter to ensure that we don't spin forever.
  auto limiter = LoopLimiter<1>::WithDuration(timeout);

  // |requesting_cpu_| acts as a spinlock.  The lock is available when it
  // contains the value INVALID_CPU.  When held, it contains the cpu_num_t of
  // the holder.
  cpu_num_t cpu = arch_curr_cpu_num();
  cpu_num_t expected = INVALID_CPU;
  while (!requesting_cpu_.compare_exchange_strong(expected, cpu, ktl::memory_order_acquire)) {
    arch::Yield();
    if (limiter.Exceeded()) {
      // The timeout has elapsed before we've acquired the lock.  Give up.
      return ZX_ERR_TIMED_OUT;
    }
    expected = INVALID_CPU;
  }

  // We got the lock.  Issue the request.
  target_cpu_.store(target_cpu, ktl::memory_order_release);

  // The exchange is now committed.  If the target does not respond, we cannot
  // release the lock.

  // Notify the target.
  notify_fn_(target_cpu);

  // Wait for the reply or a timeout.
  while (target_cpu_.load(ktl::memory_order_acquire) != INVALID_CPU) {
    arch::Yield();
    if (limiter.Exceeded()) {
      // The timeout has elapsed before we've gotten a reply.  We cannot release
      // the lock because we don't know if the target has observed the request
      // and is in the process of responding.
      //
      // TODO(maniscalco): Use a sentinel value to poison the lock.  That way
      // subsequent requesters spinning to acquire will see the sentinel value
      // and bail out early.  Consider using SMP_MAX_CPUS.
      return ZX_ERR_TIMED_OUT;
    }
  }

  // Copy the reply and release the "lock".
  context = storage_;
  requesting_cpu_.store(INVALID_CPU, ktl::memory_order_release);

  return ZX_OK;
}

template <typename NotifyFn>
void CpuContextExchange<NotifyFn>::HandleRequest(vaddr_t fp, const iframe_t& frame) {
  // This method is designed to be called from hard IRQ context, specifically an
  // NMI handler.  It's critical that interrupts remain disabled and that we
  // don't spend too much time here.
  DEBUG_ASSERT(arch_ints_disabled());

  // Is the request for us?
  if (target_cpu_.load(ktl::memory_order_acquire) != arch_curr_cpu_num()) {
    return;
  }

  storage_.frame = frame;
  Thread::Current::GetBacktrace(fp, storage_.backtrace);

  // Signal that we're done.
  target_cpu_.store(INVALID_CPU, ktl::memory_order_release);
}

#endif  // ZIRCON_KERNEL_LIB_BACKTRACE_INCLUDE_LIB_BACKTRACE_CPU_CONTEXT_EXCHANGE_H_
