// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/event.h>
#include <zircon/types.h>
#include <fbl/atomic.h>
#include <fbl/mutex.h>
#include <fbl/vector.h>
#include <object/dispatcher.h>
#include <sys/types.h>
#include <object/port_dispatcher.h>
#include <kernel/auto_lock.h>

#define SIGNAL_MASK(signal) (1ul << (signal))

enum class InterruptState {
    WAITING              = 0,
    DESTROYED            = 1,
    TRIGGERED            = 2,
    NEEDACK              = 3,
    IDLE                 = 4,
};

// Note that unlike most Dispatcher subclasses, this one is further
// subclassed, and so cannot be final.
class InterruptDispatcher : public SoloDispatcher {
public:
    InterruptDispatcher& operator=(const InterruptDispatcher &) = delete;

    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_INTERRUPT; }

    virtual zx_status_t Bind(uint32_t slot, uint32_t vector, uint32_t options) = 0;

#if ENABLE_NEW_IRQ_API
    zx_status_t WaitForInterrupt(zx_time_t* out_timestamp);
    zx_status_t UserSignal(zx_time_t timestamp);
    zx_status_t Destroy();
    void InterruptHandler(bool pci);
#else
    // Signal the IRQ from non-IRQ state in response to a user-land request.
    zx_status_t UserSignal(uint32_t slot, zx_time_t timestamp);
    zx_status_t WaitForInterrupt(uint64_t* out_slots);
#endif
    zx_status_t GetTimeStamp(uint32_t slot, zx_time_t* out_timestamp);

protected:
    virtual void MaskInterrupt(uint32_t vector) = 0;
    virtual void UnmaskInterrupt(uint32_t vector) = 0;
    virtual zx_status_t RegisterInterruptHandler(uint32_t vector, void* data) = 0;
    virtual void UnregisterInterruptHandler(uint32_t vector) = 0;

    zx_status_t AddSlotLocked(uint32_t slot, uint32_t vector, uint32_t flags) TA_REQ(get_lock());
    zx_status_t RegisterInterruptHandler_HelperLocked(uint32_t vector, uint32_t flags) TA_REQ(get_lock());

    InterruptDispatcher();

    void on_zero_handles() final;


#if ENABLE_NEW_IRQ_API
    int Signal() {
        return event_signal_etc(&event_, true, ZX_OK);
    }

    zx_status_t SendPacket(zx_time_t timestamp);
#else
    int Signal(uint64_t signals, bool reschedule) {
        signals_.fetch_or(signals);
        return event_signal_etc(&event_, reschedule, ZX_OK);
    }
#endif

    // slot used for canceling wait on last handle closed
    static constexpr uint64_t INTERRUPT_CANCEL_MASK = SIGNAL_MASK(63);

    // Bits for Interrupt.flags
    static constexpr uint32_t INTERRUPT_VIRTUAL         = (1u << 0);
    static constexpr uint32_t INTERRUPT_UNMASK_PREWAIT  = (1u << 1);
    static constexpr uint32_t INTERRUPT_MASK_POSTWAIT   = (1u << 2);

#if !ENABLE_NEW_IRQ_API
    struct Interrupt {
        InterruptDispatcher* dispatcher;
        volatile zx_time_t timestamp;
        uint16_t vector;
        uint16_t slot;
        uint32_t flags;
    };
#endif

private:
    event_t event_;

#if ENABLE_NEW_IRQ_API
    zx_time_t timestamp_ TA_GUARDED(spinlock_);
    // Interrupt Flags
    uint32_t flags_;
    // Interrupt Vector
    uint16_t vector_;
    // Current state of the interrupt object
    InterruptState state_ TA_GUARDED(spinlock_);
    // Port bind status
    bool is_bound_ TA_GUARDED(spinlock_);
    // Controls the access to Interrupt properties
    SpinLock spinlock_;
#else
    fbl::Vector<Interrupt> interrupts_;

    // slot to interrupts_ index map
    uint8_t slot_map_[ZX_INTERRUPT_MAX_SLOTS + 1];

    // current signaled slots
    fbl::atomic<uint64_t> signals_;
    // the signaled slots most recently returned from WaitForInterrupt()
    fbl::atomic<uint64_t> reported_signals_;
#endif

};
