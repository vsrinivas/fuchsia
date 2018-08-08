// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "performance_counters.h"

#include "registers.h"

namespace {
constexpr uint32_t kPerfBufferSize = PAGE_SIZE * 4;

// Start of the buffer in the GPU address space.
constexpr uint32_t kPerfBufferStartOffset = PAGE_SIZE;
} // namespace

bool PerformanceCounters::Enable()
{
    if (counter_state_ != PerformanceCounterState::kDisabled) {
        magma::log(magma::LOG_WARNING, "Can't enable performance counters from state %d\n",
                   static_cast<int>(counter_state_));
        return false;
    }
    magma::log(magma::LOG_INFO, "Enabling performance counters\n");
    if (!connection_) {
        auto connection = MsdArmConnection::Create(0xffffffff, owner_->connection_owner());
        if (!connection) {
            return DRETF(false, "Unable to create perf counter connection");
        }
        std::shared_ptr<MsdArmBuffer> buffer(
            MsdArmBuffer::Create(kPerfBufferSize, "performance_counter_buffer"));
        if (!buffer) {
            return DRETF(false, "Unable to create perf counter buffer");
        }
        auto gpu_mapping =
            std::make_unique<GpuMapping>(kPerfBufferStartOffset, 0, kPerfBufferSize,
                                         MAGMA_GPU_MAP_FLAG_WRITE | MAGMA_GPU_MAP_FLAG_READ |
                                             kMagmaArmMaliGpuMapFlagInnerShareable,
                                         connection.get(), buffer);
        bool result = connection->AddMapping(std::move(gpu_mapping));
        if (!result) {
            return DRETF(false, "Unable to map perf counter buffer");
        }
        result = buffer->SetCommittedPages(0, kPerfBufferSize / PAGE_SIZE);
        if (!connection) {
            return DRETF(false, "Unable to commit pages for perf counter buffer");
        }
        // Keep mapped on the CPU forever.
        void* cpu_map;
        if (!buffer->platform_buffer()->MapCpu(&cpu_map)) {
            return DRETF(false, "Failed to map perf counter buffer\n");
        }
        auto mapping = owner_->address_manager()->AllocateMappingForAddressSpace(connection);
        if (!mapping) {
            return DRETF(false, "Unable to map perf counter address space to GPU");
        }
        buffer->platform_buffer()->CleanCache(0, kPerfBufferSize, true);
        connection_ = connection;
        buffer_ = buffer;
        address_mapping_ = mapping;
    }

    auto base = registers::PerformanceCounterBase::Get().FromValue(kPerfBufferStartOffset);
    base.WriteTo(owner_->register_io());
    last_perf_base_ =
        registers::PerformanceCounterBase::Get().ReadFrom(owner_->register_io()).reg_value();

    // Enable every performance counter
    registers::PerformanceCounterJmEnable::Get()
        .FromValue(0xffffffffu)
        .WriteTo(owner_->register_io());
    registers::PerformanceCounterTilerEnable::Get()
        .FromValue(0xffffffffu)
        .WriteTo(owner_->register_io());
    registers::PerformanceCounterShaderEnable::Get()
        .FromValue(0xffffffffu)
        .WriteTo(owner_->register_io());
    registers::PerformanceCounterMmuL2Enable::Get()
        .FromValue(0xffffffffu)
        .WriteTo(owner_->register_io());

    auto config = registers::PerformanceCounterConfig::Get().FromValue(0);
    config.address_space().set(address_mapping_->slot_number());
    config.mode().set(registers::PerformanceCounterConfig::kModeManual);
    config.WriteTo(owner_->register_io());
    counter_state_ = PerformanceCounterState::kEnabled;
    enable_time_ = std::chrono::steady_clock::now();
    return true;
}

bool PerformanceCounters::TriggerRead(bool keep_enabled)
{
    if (counter_state_ != PerformanceCounterState::kEnabled) {
        magma::log(magma::LOG_WARNING, "Can't trigger performance counters from state %d\n",
                   static_cast<int>(counter_state_));
        return false;
    }
    magma::log(magma::LOG_INFO, "Triggering performance counter read\n");
    last_perf_base_ =
        registers::PerformanceCounterBase::Get().ReadFrom(owner_->register_io()).reg_value();
    owner_->register_io()->Write32(registers::GpuCommand::kOffset,
                                   registers::GpuCommand::kCmdSamplePerformanceCounters);
    counter_state_ = PerformanceCounterState::kTriggered;
    enable_after_read_ = keep_enabled;
    return true;
}

std::vector<uint32_t> PerformanceCounters::ReadCompleted(uint64_t* duration_ms_out)
{
    std::vector<uint32_t> output;
    if (counter_state_ != PerformanceCounterState::kTriggered) {
        DLOG("Can't trigger performance counters from state %d\n",
             static_cast<int>(counter_state_));
        return output;
    }
    uint64_t new_base =
        registers::PerformanceCounterBase::Get().ReadFrom(owner_->register_io()).reg_value();

    DASSERT(new_base >= last_perf_base_);
    DASSERT(new_base <= kPerfBufferSize + kPerfBufferStartOffset);

    void* mapped_data;
    uint64_t base = last_perf_base_ - kPerfBufferStartOffset;
    buffer_->platform_buffer()->CleanCache(base, kPerfBufferSize, true);
    bool success = buffer_->platform_buffer()->MapCpu(&mapped_data);
    DASSERT(success);
    auto perf_registers = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(mapped_data) + base);

    auto now = std::chrono::steady_clock::now();
    *duration_ms_out =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - enable_time_).count();

    for (uint32_t i = 0; i < (new_base - last_perf_base_) / sizeof(uint32_t); ++i) {
        output.push_back(perf_registers[i]);
    }

    buffer_->platform_buffer()->UnmapCpu();
    auto config = registers::PerformanceCounterConfig::Get().FromValue(0);
    config.address_space().set(address_mapping_->slot_number());
    config.mode().set(registers::PerformanceCounterConfig::kModeDisabled);
    config.WriteTo(owner_->register_io());
    counter_state_ = PerformanceCounterState::kDisabled;

    if (enable_after_read_) {
        // Reading from the performance counters clears them but leaves them
        // enabled, so just setting the state to enabled would normally work. However,
        // the base register address changes every time a read happens, so we
        // need to temporarily disable them to reset that address so we don't
        // overflow the buffer.
        Enable();
    }

    return output;
}
