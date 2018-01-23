// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERFORMANCE_COUNTERS_H
#define PERFORMANCE_COUNTERS_H

#include "address_manager.h"
#include "msd_arm_buffer.h"
#include "msd_arm_connection.h"

class PerformanceCounters {
public:
    class Owner {
    public:
        virtual magma::RegisterIo* register_io() = 0;
        virtual AddressManager* address_manager() = 0;
        virtual MsdArmConnection::Owner* connection_owner() = 0;
    };

    PerformanceCounters(Owner* owner) : owner_(owner) {}

    bool Enable();
    bool TriggerRead(bool keep_enabled);
    std::vector<uint32_t> ReadCompleted(uint64_t* duration_ms_out);

private:
    friend class PerformanceCounterTest;
    enum class PerformanceCounterState {
        kDisabled,
        kEnabled,
        kTriggered,
    };

    Owner* owner_;
    PerformanceCounterState counter_state_ = PerformanceCounterState::kDisabled;
    std::shared_ptr<MsdArmConnection> connection_;
    std::shared_ptr<MsdArmBuffer> buffer_;
    std::shared_ptr<AddressSlotMapping> address_mapping_;
    uint64_t last_perf_base_ = 0;
    std::chrono::steady_clock::time_point enable_time_;
    bool enable_after_read_ = false;
};

#endif // PERFORMANCE_COUNTERS_H_
