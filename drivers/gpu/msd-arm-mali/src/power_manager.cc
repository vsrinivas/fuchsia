// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager.h"

#include "registers.h"

PowerManager::PowerManager(RegisterIo* io)
{
    // Initialize current set of running cores.
    ReceivedPowerInterrupt(io);
    last_check_time_ = std::chrono::steady_clock::now();
}

void PowerManager::EnableCores(RegisterIo* io, uint64_t shader_bitmask)
{
    registers::CoreReadyState::WriteState(io, registers::CoreReadyState::CoreType::kShader,
                                          registers::CoreReadyState::ActionType::kActionPowerOn,
                                          shader_bitmask);
    registers::CoreReadyState::WriteState(io, registers::CoreReadyState::CoreType::kL2,
                                          registers::CoreReadyState::ActionType::kActionPowerOn, 1);
    registers::CoreReadyState::WriteState(io, registers::CoreReadyState::CoreType::kTiler,
                                          registers::CoreReadyState::ActionType::kActionPowerOn, 1);
}

void PowerManager::ReceivedPowerInterrupt(RegisterIo* io)
{
    tiler_ready_status_ =
        registers::CoreReadyState::ReadBitmask(io, registers::CoreReadyState::CoreType::kTiler,
                                               registers::CoreReadyState::StatusType::kReady);
    l2_ready_status_ =
        registers::CoreReadyState::ReadBitmask(io, registers::CoreReadyState::CoreType::kL2,
                                               registers::CoreReadyState::StatusType::kReady);
    shader_ready_status_ =
        registers::CoreReadyState::ReadBitmask(io, registers::CoreReadyState::CoreType::kShader,
                                               registers::CoreReadyState::StatusType::kReady);
}

void PowerManager::UpdateGpuActive(bool active)
{
    auto now = std::chrono::steady_clock::now();
    std::chrono::steady_clock::duration total_time = now - last_check_time_;
    constexpr uint32_t kMemoryMilliseconds = 100;
    std::chrono::milliseconds memory_duration(kMemoryMilliseconds);

    // Ignore long periods of inactive time.
    if (total_time > memory_duration)
        total_time = memory_duration;

    std::chrono::steady_clock::duration active_time =
        gpu_active_ ? total_time : std::chrono::steady_clock::duration(0);

    constexpr uint32_t kBucketLengthMilliseconds = 50;
    bool coalesced = false;
    if (!time_periods_.empty()) {
        auto start_time = time_periods_.back().end_time - time_periods_.back().total_time;
        if (now - start_time < std::chrono::milliseconds(kBucketLengthMilliseconds)) {
            coalesced = true;
            time_periods_.back().end_time = now;
            time_periods_.back().total_time += total_time;
            time_periods_.back().active_time += active_time;
        }
    }

    if (!coalesced)
        time_periods_.push_back(TimePeriod{now, total_time, active_time});

    while (!time_periods_.empty() && (now - time_periods_.front().end_time > memory_duration)) {
        time_periods_.pop_front();
    }

    last_check_time_ = now;
    gpu_active_ = active;
}

void PowerManager::GetGpuActiveInfo(std::chrono::steady_clock::duration* total_time_out,
                                    std::chrono::steady_clock::duration* active_time_out)
{
    UpdateGpuActive(gpu_active_);

    std::chrono::steady_clock::duration total_time_accumulate(0);
    std::chrono::steady_clock::duration active_time_accumulate(0);
    for (auto& period : time_periods_) {
        total_time_accumulate += period.total_time;
        active_time_accumulate += period.active_time;
    }

    *total_time_out = total_time_accumulate;
    *active_time_out = active_time_accumulate;
}
