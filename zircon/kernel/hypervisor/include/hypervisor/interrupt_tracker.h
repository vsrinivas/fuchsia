// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_INTERRUPT_TRACKER_H_
#define ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_INTERRUPT_TRACKER_H_

#include <lib/fit/defer.h>
#include <lib/ktrace.h>
#include <lib/zx/result.h>

#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <hypervisor/ktrace.h>
#include <hypervisor/state_invalidator.h>
#include <kernel/auto_lock.h>
#include <kernel/event.h>

namespace hypervisor {

template <uint32_t N>
class InterruptBitmap {
 public:
  InterruptBitmap() {
    zx_status_t result = bitmap_.Reset(N);
    // `bitmap_` uses static storage, so `Reset` cannot fail.
    DEBUG_ASSERT(result == ZX_OK);
  }

  bool Get(uint32_t vector) const {
    if (vector >= N) {
      DEBUG_ASSERT(false);
      return false;
    }
    return bitmap_.GetOne(vector);
  }

  void Set(uint32_t vector) {
    if (vector >= N) {
      DEBUG_ASSERT(false);
      return;
    }
    bitmap_.SetOne(vector);
  }

  void Clear(uint32_t min, uint32_t max) {
    if (max < min || max > N) {
      DEBUG_ASSERT(false);
      return;
    }
    bitmap_.Clear(min, max);
  }

  bool Scan(uint32_t* vector) {
    size_t bitoff;
#if ARCH_ARM64
    bool is_empty = bitmap_.Scan(0, N, false, &bitoff);
#elif ARCH_X86
    bool is_empty = bitmap_.ReverseScan(0, N, false, &bitoff);
#endif
    if (is_empty) {
      return false;
    }
    *vector = static_cast<uint32_t>(bitoff);
    return true;
  }

 private:
  bitmap::RawBitmapGeneric<bitmap::FixedStorage<N>> bitmap_;
};

// |N| is the maximum number of interrupts to be tracked.
template <uint32_t N>
class InterruptTracker {
 public:
  // Returns whether there are pending interrupts.
  bool Pending() {
    uint32_t vector;
    Guard<SpinLock, IrqSave> lock{&lock_};
    return bitmap_.Scan(&vector);
  }

  // Clears all vectors in the range [min, max).
  void Clear(uint32_t min, uint32_t max) {
    Guard<SpinLock, IrqSave> lock{&lock_};
    bitmap_.Clear(min, max);
  }

  // Pops the specified vector, if it is pending.
  bool TryPop(uint32_t vector) {
    Guard<SpinLock, IrqSave> lock{&lock_};
    if (bitmap_.Get(vector)) {
      bitmap_.Clear(vector, vector + 1);
      return true;
    }
    return false;
  }

  // Pops the highest priority interrupt.
  bool Pop(uint32_t* vector) {
    Guard<SpinLock, IrqSave> lock{&lock_};
    if (bitmap_.Scan(vector)) {
      bitmap_.Clear(*vector, *vector + 1);
      return true;
    }
    return false;
  }

  // Tracks the given interrupt.
  void Track(uint32_t vector) {
    Guard<SpinLock, IrqSave> lock{&lock_};
    bitmap_.Set(vector);
  }

  // Tracks the given interrupt, and signals any waiters.
  void Interrupt(uint32_t vector) {
    Track(vector);
    event_.Signal();
  }

  // Cancels a wait for an interrupt.
  //
  // We signal `ZX_ERR_INTERNAL_INTR_RETRY`, so that if the status is propagated
  // to the syscall-layer, we will retry the syscall.
  void Cancel() { event_.Signal(ZX_ERR_INTERNAL_INTR_RETRY); }

  // Waits for an interrupt.
  zx::result<> Wait(zx_time_t deadline, StateInvalidator* invalidator = nullptr) {
    if (invalidator != nullptr) {
      invalidator->Invalidate();
    }
    ktrace_vcpu(TAG_VCPU_BLOCK, VCPU_INTERRUPT);
    auto defer = fit::defer([] { ktrace_vcpu(TAG_VCPU_UNBLOCK, VCPU_INTERRUPT); });
    do {
      zx_status_t status = event_.Wait(Deadline::no_slack(deadline));
      switch (status) {
        case ZX_OK:
          continue;
        case ZX_ERR_TIMED_OUT:
          // If the event timed out, return ZX_OK to resume the VCPU.
          return zx::ok();
        default:
          // Otherwise, return the status.
          return zx::error(status);
      }
    } while (!Pending());
    return zx::ok();
  }

 private:
  AutounsignalEvent event_;
  DECLARE_SPINLOCK(InterruptTracker) lock_;
  InterruptBitmap<N> bitmap_ TA_GUARDED(lock_);
};

}  // namespace hypervisor

#endif  // ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_INTERRUPT_TRACKER_H_
