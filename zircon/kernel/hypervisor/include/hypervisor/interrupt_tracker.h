// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_INTERRUPT_TRACKER_H_
#define ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_INTERRUPT_TRACKER_H_

#include <lib/ktrace.h>

#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <hypervisor/ktrace.h>
#include <hypervisor/state_invalidator.h>
#include <kernel/auto_lock.h>
#include <kernel/event.h>

namespace hypervisor {

// InterruptBitmap relies on these precise enum values, do not modify without adjusting below.
enum class InterruptType : uint8_t {
  INACTIVE = 0,
  VIRTUAL = 1,
  PHYSICAL = 2,
};

template <uint32_t N>
class InterruptBitmap {
 public:
  zx_status_t Init() { return bitmap_.Reset(kNumBits); }

  InterruptType Get(uint32_t vector) const {
    if (vector >= N) {
      DEBUG_ASSERT(false);
      return InterruptType::INACTIVE;
    }
    size_t bitoff = vector * 2;
    size_t first;
    bool inactive = bitmap_.Scan(bitoff, bitoff + 2, false, &first);
    if (inactive) {
      return InterruptType::INACTIVE;
    }
    return bitoff == first ? InterruptType::VIRTUAL : InterruptType::PHYSICAL;
  }

  void Set(uint32_t vector, InterruptType type) {
    if (vector >= N) {
      DEBUG_ASSERT(false);
      return;
    }
    size_t bitoff = vector * 2;
    bitmap_.Clear(bitoff, bitoff + 2);
    if (type != InterruptType::INACTIVE) {
      auto state_bit = static_cast<size_t>(type) - 1;
      bitmap_.SetOne(bitoff + state_bit);
    }
  }

  void Clear(uint32_t min, uint32_t max) {
    if (max < min || max >= N) {
      DEBUG_ASSERT(false);
      return;
    }
    bitmap_.Clear(min * 2, max * 2);
  }

  InterruptType Scan(uint32_t* vector) {
    size_t bitoff;
#if ARCH_ARM64
    bool is_empty = bitmap_.Scan(0, kNumBits, false, &bitoff);
#elif ARCH_X86
    bool is_empty = bitmap_.ReverseScan(0, kNumBits, false, &bitoff);
#endif
    if (is_empty) {
      return InterruptType::INACTIVE;
    }
    *vector = static_cast<uint32_t>(bitoff / 2);
    if (bitoff % 2 == 0) {
      return InterruptType::VIRTUAL;
    } else {
      return InterruptType::PHYSICAL;
    }
  }

 private:
  static constexpr uint32_t kNumBits = N * 2;
  bitmap::RawBitmapGeneric<bitmap::FixedStorage<kNumBits>> bitmap_;
};

// |N| is the maximum number of interrupts to be tracked.
template <uint32_t N>
class InterruptTracker {
 public:
  zx_status_t Init() {
    event_init(&event_, false, EVENT_FLAG_AUTOUNSIGNAL);
    Guard<SpinLock, IrqSave> lock{&lock_};
    return bitmap_.Init();
  }

  // Returns whether there are pending interrupts.
  bool Pending() {
    uint32_t vector;
    Guard<SpinLock, IrqSave> lock{&lock_};
    return bitmap_.Scan(&vector) != InterruptType::INACTIVE;
  }

  // Clears all vectors in the range [min, max).
  void Clear(uint32_t min, uint32_t max) {
    Guard<SpinLock, IrqSave> lock{&lock_};
    bitmap_.Clear(min, max);
  }

  // Pops the specified vector, if it is pending.
  InterruptType TryPop(uint32_t vector) {
    Guard<SpinLock, IrqSave> lock{&lock_};
    InterruptType type = bitmap_.Get(vector);
    if (type != InterruptType::INACTIVE) {
      bitmap_.Set(vector, InterruptType::INACTIVE);
    }
    return type;
  }

  // Pops the highest priority interrupt.
  InterruptType Pop(uint32_t* vector) {
    Guard<SpinLock, IrqSave> lock{&lock_};
    InterruptType type = bitmap_.Scan(vector);
    if (type != InterruptType::INACTIVE) {
      bitmap_.Set(*vector, InterruptType::INACTIVE);
    }
    return type;
  }

  // Tracks the given interrupt.
  void Track(uint32_t vector, InterruptType type) {
    Guard<SpinLock, IrqSave> lock{&lock_};
    bitmap_.Set(vector, type);
  }

  // Tracks the given interrupt, and signals any waiters.
  bool Interrupt(uint32_t vector, InterruptType type) {
    Track(vector, type);
    return event_signal(&event_, true) > 0;
  }

  // Tracks the given virtual interrupt, and signals any waiters.
  bool VirtualInterrupt(uint32_t vector) {
    return Interrupt(vector, hypervisor::InterruptType::VIRTUAL);
  }

  // Waits for an interrupt.
  zx_status_t Wait(zx_time_t deadline, StateInvalidator* invalidator) {
    if (invalidator != nullptr) {
      invalidator->Invalidate();
    }
    ktrace_vcpu(TAG_VCPU_BLOCK, VCPU_INTERRUPT);
    do {
      zx_status_t status = event_wait_deadline(&event_, deadline, true);
      if (status == ZX_ERR_TIMED_OUT) {
        break;
      } else if (status != ZX_OK) {
        ktrace_vcpu(TAG_VCPU_UNBLOCK, VCPU_INTERRUPT);
        return ZX_ERR_CANCELED;
      }
    } while (!Pending());
    ktrace_vcpu(TAG_VCPU_UNBLOCK, VCPU_INTERRUPT);
    return ZX_OK;
  }

 private:
  event_t event_;
  DECLARE_SPINLOCK(InterruptTracker) lock_;
  InterruptBitmap<N> bitmap_ TA_GUARDED(lock_);
};

}  // namespace hypervisor

#endif  // ZIRCON_KERNEL_HYPERVISOR_INCLUDE_HYPERVISOR_INTERRUPT_TRACKER_H_
