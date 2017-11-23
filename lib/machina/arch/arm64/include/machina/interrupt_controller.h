// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hypervisor/io.h>

class Guest;

// Target CPU for the software-generated interrupt.
enum class InterruptTarget {
    MASK            = 0b00,
    ALL_BUT_LOCAL   = 0b01,
    LOCAL           = 0b10,
};

// Software-generated interrupt received by the GIC distributor.
struct SoftwareGeneratedInterrupt {
    InterruptTarget target;
    uint8_t cpu_mask;
    bool non_secure;
    uint8_t vector;

    SoftwareGeneratedInterrupt(uint32_t sgi);
};

// Implements GIC distributor.
class GicDistributor : public IoHandler {
public:
    zx_status_t Init(Guest* guest);

    zx_status_t Read(uint64_t addr, IoValue* value) const override;
    zx_status_t Write(uint64_t addr, const IoValue& value) override;

    zx_status_t Interrupt(uint32_t global_irq) const;
};

using InterruptController = GicDistributor;
