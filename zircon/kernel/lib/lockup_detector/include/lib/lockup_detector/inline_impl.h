// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_LOCKUP_DETECTOR_INCLUDE_LIB_LOCKUP_DETECTOR_INLINE_IMPL_H_
#define ZIRCON_KERNEL_LIB_LOCKUP_DETECTOR_INCLUDE_LIB_LOCKUP_DETECTOR_INLINE_IMPL_H_

#include <inttypes.h>
#include <lib/lockup_detector/state.h>

#include <arch/ops.h>
#include <ktl/atomic.h>
#include <ktl/forward.h>

// This file contains inline implementations of lockup_detector functions.

namespace lockup_internal {

// Enter a critical section.
//
// Returns true if this is the outermost critical section.
//
// Don't forget to call |CallIfOuterAndLeave| (regardless of this function's return value).
inline bool Enter(LockupDetectorState& state, const char* name) {
  auto& cs_state = state.critical_section;

  // We must maintain the invariant that if a call to `Enter` increments the depth, the matching
  // call to `CallIfOuterAndLeave` decrements it.  The most reliable way to accomplish that is to
  // always increment and always decrement.
  cs_state.depth++;
  if (cs_state.depth != 1) {
    return false;
  }

  // This is the outermost critical section.  However, we may be racing with an interrupt handler
  // that may call into `Enter`.  Use a compiler fence to ensure that the compiler cannot
  // reorder upcoming stores to precede the depth=1 store above.  If the compiler were to make such
  // a reordering the `Enter` call made by the interrupt handler may incorrectly believe
  // its critical section is the outermost critical section because it has not seen our depth=1
  // store.
  //
  // Note, there is a small gap here where the CriticalSectionLockupChecker may fail to notice a
  // lockup (though the HeartbeatLockupChecker may still detect it).  Consider the following:
  //
  //   1. We have stored depth=1, but not yet stored begin_ticks.
  //   2. An interrupt fires and the handler calls `Enter`.
  //   3. The handler see that depth is 1 so it does nothing.
  //   4. The CPU enters an infinite loop.
  //   5. CriticalSectionLockupChecker sees that begin_time has not be set so it assumes the CPU is
  //      not in a section.
  //
  // If interrupts are enabled at the time of the infinite loop in step 4, the system may never
  // detect the problem.  If instead of an infinite loop we just spend a lot of time in the
  // interrupt handler, we may never know it because the begin_time would only get set by the outer
  // critical section *after* returning from the handler.
  //
  // One way to close the gap would be to use begin_ticks rather than depth to determine if we're
  // already critical section.  Instead of storing begin_ticks when depth=1, we would store
  // begin_ticks only if it's currently unset (i.e. use an atomic compare and exchange with an
  // expected value of 0).  However, this would increase the cost of critical section
  // instrumentation.  Because the gap is small and we have heartbeats we have chosen to live with
  // it rather than pay the price of atomic compare and exchange.
  ktl::atomic_signal_fence(std::memory_order_seq_cst);

  return true;
}

// Call |func| if in the outmost critical section then leave the current critical section.
template <typename Func>
inline void CallIfOuterAndLeave(LockupDetectorState& state, Func&& func) {
  auto& cs_state = state.critical_section;
  if (cs_state.depth == 1) {
    // This is the outermost critical section.  However, we may be racing with an interrupt handler
    // that may call into `Enter`.  Use a compiler fence to ensure that any operations performed by
    // |func| cannot be compiler reordered to preceded the depth operations above.
    ktl::atomic_signal_fence(std::memory_order_seq_cst);
    ktl::forward<Func>(func)(state);
  }
  DEBUG_ASSERT(cs_state.depth > 0);
  cs_state.depth--;
}

}  // namespace lockup_internal

inline void lockup_begin(const char* name) {
  LockupDetectorState& state = gLockupDetectorPerCpuState[arch_curr_cpu_num()];
  const bool outermost = lockup_internal::Enter(state, name);
  if (likely(outermost)) {
    auto& cs_state = state.critical_section;
    // We're using memory_order_relaxed instead of memory_order_release to
    // minimize performance impact.  As a result, HeartbeatLockupChecker and
    // CriticalSectionLockupChecker may see stale name values because there is
    // nothing for them to synchronize-with.
    cs_state.name.store(name, ktl::memory_order_relaxed);
  }
}

inline void lockup_end() {
  LockupDetectorState& state = gLockupDetectorPerCpuState[arch_curr_cpu_num()];
  lockup_internal::CallIfOuterAndLeave(state, [](LockupDetectorState& state) {
    auto& cs_state = state.critical_section;
    // See comment in lockup_begin at the point where name is stored.
    cs_state.name.store(nullptr, ktl::memory_order_relaxed);
  });
}

#endif  // ZIRCON_KERNEL_LIB_LOCKUP_DETECTOR_INCLUDE_LIB_LOCKUP_DETECTOR_INLINE_IMPL_H_
