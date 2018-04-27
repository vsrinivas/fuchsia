// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/event.h>
#include <zircon/types.h>
#include <fbl/mutex.h>
#include <object/dispatcher.h>
#include <sys/types.h>
#include <object/port_dispatcher.h>
#include <kernel/auto_lock.h>

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
    zx_status_t WaitForInterrupt(zx_time_t* out_timestamp);
    zx_status_t UserSignal(zx_time_t timestamp);
    zx_status_t Destroy();
    void InterruptHandler(bool pci);

protected:
    virtual void MaskInterrupt(uint32_t vector) = 0;
    virtual void UnmaskInterrupt(uint32_t vector) = 0;
    virtual zx_status_t RegisterInterruptHandler(uint32_t vector, void* data) = 0;
    virtual void UnregisterInterruptHandler(uint32_t vector) = 0;
    zx_status_t RegisterInterruptHandler_HelperLocked(uint32_t vector,
                        uint32_t flags) TA_REQ(get_lock());
    InterruptDispatcher();
    void on_zero_handles() final;
    int Signal() {
        return event_signal_etc(&event_, true, ZX_OK);
    }
    zx_status_t SendPacket(zx_time_t timestamp);
    // Bits for Interrupt.flags
    static constexpr uint32_t INTERRUPT_VIRTUAL         = (1u << 0);
    static constexpr uint32_t INTERRUPT_UNMASK_PREWAIT  = (1u << 1);
    static constexpr uint32_t INTERRUPT_MASK_POSTWAIT   = (1u << 2);

private:
    event_t event_;

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
};
