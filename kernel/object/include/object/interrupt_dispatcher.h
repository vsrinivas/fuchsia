// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/event.h>
#include <kernel/spinlock.h>
#include <zircon/types.h>
#include <fbl/mutex.h>
#include <object/dispatcher.h>
#include <sys/types.h>
#include <object/port_dispatcher.h>

enum class InterruptState {
    WAITING         = 0,
    DESTROYED       = 1,
    TRIGGERED       = 2,
    NEEDACK         = 3,
    IDLE            = 4,
};

// Note that unlike most Dispatcher subclasses, this one is further
// subclassed, and so cannot be final.
class InterruptDispatcher : public SoloDispatcher<InterruptDispatcher> {
public:
    InterruptDispatcher& operator=(const InterruptDispatcher&) = delete;
    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_INTERRUPT; }
    uint32_t get_flags() const { return flags_; };

    zx_status_t WaitForInterrupt(zx_time_t* out_timestamp);
    zx_status_t Trigger(zx_time_t timestamp);
    zx_status_t Ack();
    zx_status_t Destroy();
    void InterruptHandler();
    zx_status_t Bind(fbl::RefPtr<PortDispatcher> port_dispatcher,
                     fbl::RefPtr<InterruptDispatcher> interrupt, uint64_t key);

protected:
    virtual void MaskInterrupt() = 0;
    virtual void UnmaskInterrupt() = 0;
    virtual void UnregisterInterruptHandler() = 0;
    InterruptDispatcher();
    void on_zero_handles() final;
    void Signal() {
        event_signal_etc(&event_, true, ZX_OK);
    }
    void set_flags(uint32_t flags) { flags_ = flags; }
    bool SendPacketLocked(zx_time_t timestamp) TA_REQ(spinlock_);
    // Bits for Interrupt.flags
    static constexpr uint32_t INTERRUPT_VIRTUAL         = (1u << 0);
    static constexpr uint32_t INTERRUPT_UNMASK_PREWAIT  = (1u << 1);
    static constexpr uint32_t INTERRUPT_MASK_POSTWAIT   = (1u << 2);

private:
    event_t event_;

    // Interrupt Flags
    uint32_t flags_;

    zx_time_t timestamp_ TA_GUARDED(spinlock_);
    // Current state of the interrupt object
    InterruptState state_ TA_GUARDED(spinlock_);
    PortInterruptPacket port_packet_ TA_GUARDED(spinlock_) = {};
    fbl::RefPtr<PortDispatcher> port_dispatcher_ TA_GUARDED(spinlock_);

    // Controls the access to Interrupt properties
    DECLARE_SPINLOCK(InterruptDispatcher) spinlock_;

};
