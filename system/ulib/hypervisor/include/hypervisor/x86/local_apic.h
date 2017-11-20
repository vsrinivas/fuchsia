// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/mutex.h>
#include <hypervisor/io.h>
#include <zircon/thread_annotations.h>

class Guest;
class Vcpu;

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
    Registers* registers_ TA_GUARDED(mutex_);
};
