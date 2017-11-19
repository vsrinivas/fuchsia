// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <async/task.h>
#include <async/loop.h>
#include <fbl/mutex.h>
#include <hypervisor/io.h>
#include <zircon/compiler.h>

class Guest;
class Vcpu;
class LocalApic;

// Local APIC Timer implementation
class LocalApicTimer {
public:
    LocalApicTimer(LocalApic* apic);
    ~LocalApicTimer();

    zx_status_t WriteLvt(uint32_t value);
    uint32_t ReadLvt() const;

    zx_status_t WriteDcr(uint32_t value);
    uint32_t ReadDcr() const;

    zx_status_t WriteIcr(uint32_t value);
    uint32_t ReadIcr() const;

    uint32_t ReadCcr() const;

private:
    void UpdateLocked(zx_time_t now) __TA_REQUIRES(mutex_) ;
    void Interrupt() __TA_EXCLUDES(mutex_);

    enum class Mode {
        OneShot = 0,
        Periodic = 1,
        TscDeadline = 2,
    };

    mutable fbl::Mutex mutex_;

    uint32_t divisor_shift_ __TA_GUARDED(mutex_) = 0;
    uint32_t vector_ __TA_GUARDED(mutex_) = 0;
    uint32_t initial_count_ __TA_GUARDED(mutex_) = 0;
    zx_time_t reset_time_ __TA_GUARDED(mutex_) = 0;
    zx_time_t expire_time_ __TA_GUARDED(mutex_) = 0;
    bool masked_ __TA_GUARDED(mutex_) = true;
    Mode mode_ __TA_GUARDED(mutex_) = Mode::OneShot;

    LocalApic* apic_;
    async::Task interrupt_ __TA_GUARDED(mutex_);
    async::Loop loop_;
};

// Stores the local APIC state.
class LocalApic : public IoHandler {
public:
    struct Registers;

    LocalApic(Vcpu* vcpu, uintptr_t apic_addr);

    zx_status_t Init(Guest* guest);
    zx_status_t Interrupt(uint32_t vector);

    // IoHandler interface.
    zx_status_t Read(uint64_t addr, IoValue* value) const override;
    zx_status_t Write(uint64_t addr, const IoValue& value) override;

    // Sets the value of the id register.
    void set_id(uint32_t id);
    // Read the value of the LDR register.
    uint32_t ldr() const;
    // Read the value of the DFR register.
    uint32_t dfr() const;

private:
    // VCPU associated with this APIC.
    Vcpu* vcpu_;
    mutable fbl::Mutex mutex_;
    // Register accessors.
    Registers* registers_ __TA_GUARDED(mutex_);
    LocalApicTimer timer_;
};
