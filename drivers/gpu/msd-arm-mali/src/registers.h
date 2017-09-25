// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGISTERS_H
#define REGISTERS_H

#include "magma_util/macros.h"
#include "magma_util/register_bitfields.h"
#include "magma_util/register_io.h"

namespace registers {

class GpuId : public RegisterBase {
public:
    DEF_FIELD(3, 0, version_status);
    DEF_FIELD(11, 4, minor_revision);
    DEF_FIELD(15, 12, major_revision);
    DEF_FIELD(31, 16, product_id);

    static auto Get() { return RegisterAddr<GpuId>(0); }
};

class L2Features : public RegisterBase {
public:
    DEF_FIELD(7, 0, log2_line_size);
    DEF_FIELD(15, 8, associativity);
    DEF_FIELD(23, 16, log2_cache_size);
    DEF_FIELD(31, 24, external_bus_width);

    static auto Get() { return RegisterAddr<L2Features>(0x4); }
};

class TilerFeatures : public RegisterBase {
public:
    DEF_FIELD(5, 0, log2_bin_size_bytes);
    DEF_FIELD(11, 8, max_active_levels);

    static auto Get() { return RegisterAddr<TilerFeatures>(0xc); }
};

class MemoryFeatures : public RegisterBase {
public:
    DEF_FIELD(1, 0, groups_l2_coherent);
    DEF_FIELD(11, 8, num_l2_slices_minus1);

    static auto Get() { return RegisterAddr<MemoryFeatures>(0x10); }
};

class MmuFeatures : public RegisterBase {
public:
    DEF_FIELD(7, 0, va_bits);
    DEF_FIELD(15, 8, pa_bits);

    static auto Get() { return RegisterAddr<MmuFeatures>(0x14); }
};

class ThreadFeatures : public RegisterBase {
public:
    DEF_FIELD(15, 0, max_registers);
    DEF_FIELD(23, 16, max_task_queue);
    DEF_FIELD(29, 24, max_thread_group_split);
    DEF_FIELD(31, 30, impl_tech);

    static auto Get() { return RegisterAddr<ThreadFeatures>(0xac); }
};

class GpuCommand {
public:
    static constexpr uint32_t kOffset = 0x30;

    static constexpr uint32_t kCmdNop = 0;
    static constexpr uint32_t kCmdSoftReset = 0x1;
    static constexpr uint32_t kCmdHardReset = 0x2;
    static constexpr uint32_t kCmdClearPerformanceCounters = 0x3;
    static constexpr uint32_t kCmdSamplePerformanceCounters = 0x4;
    static constexpr uint32_t kCmdCycleCountStop = 0x5;
    static constexpr uint32_t kCmdCycleCountStart = 0x6;
    static constexpr uint32_t kCmdCleanCaches = 0x7;
    static constexpr uint32_t kCmdCleanAndInvalidateCaches = 0x8;
    static constexpr uint32_t kCmdSetProtectedMode = 0x9;
};

class GpuIrqFlags : public RegisterBase {
public:
    DEF_BIT(0, gpu_fault);
    DEF_BIT(7, multiple_gpu_faults);
    DEF_BIT(8, reset_completed);
    DEF_BIT(9, power_changed_single);
    DEF_BIT(10, power_changed_all);
    DEF_BIT(16, performance_counter_sample_completed);
    DEF_BIT(17, clean_caches_completed);

    static auto GetRawStat() { return RegisterAddr<GpuIrqFlags>(0x20); }
    static auto GetIrqClear() { return RegisterAddr<GpuIrqFlags>(0x24); }
    static auto GetIrqMask() { return RegisterAddr<GpuIrqFlags>(0x28); }
    static auto GetStatus() { return RegisterAddr<GpuIrqFlags>(0x2c); }
};

class MmuIrqFlags : public RegisterBase {
public:
    DEF_FIELD(15, 0, pf_flags);
    DEF_FIELD(31, 16, bf_flags);

    static auto GetRawStat() { return RegisterAddr<MmuIrqFlags>(0x2000); }
    static auto GetIrqClear() { return RegisterAddr<MmuIrqFlags>(0x2004); }
    static auto GetIrqMask() { return RegisterAddr<MmuIrqFlags>(0x2008); }
    static auto GetStatus() { return RegisterAddr<MmuIrqFlags>(0x200c); }
};

class JobIrqFlags : public RegisterBase {
public:
    DEF_FIELD(15, 0, finished_slots);
    DEF_FIELD(31, 16, failed_slots);

    static auto GetRawStat() { return RegisterAddr<JobIrqFlags>(0x1000); }
    static auto GetIrqClear() { return RegisterAddr<JobIrqFlags>(0x1004); }
    static auto GetIrqMask() { return RegisterAddr<JobIrqFlags>(0x1008); }
    static auto GetStatus() { return RegisterAddr<JobIrqFlags>(0x100c); }
};

class CoreReadyState {
public:
    enum class CoreType {
        kShader = 0x100,
        kL2 = 0x120,
        kTiler = 0x110,
        kCoreStack = 0xe00,
    };

    enum class StatusType {
        // Read-only: the set of cores that are physically present in the
        // device.
        kPresent = 0,

        // Read-only: the set of cores that are powered on and ready to do
        // work.
        kReady = 0x40,

        // Read-only: the set of cores that are changing power states.
        kPowerTransitioning = 0x100,

        // Read-only: the set of cores that are currently executing work.
        kPowerActive = 0x100,
    };

    enum class ActionType {
        // Write-only: power on the specified set of cores.
        kActionPowerOn = 0x80,

        // Write-only: power off the specified set of cores.
        kActionPowerOff = 0xc0,
    };

    // Returns a bitmask of the cores in a specified state.
    static uint64_t ReadBitmask(RegisterIo* register_io, CoreType core_type, StatusType action_type)
    {
        DASSERT(core_type != CoreType::kCoreStack);

        uint32_t offset = static_cast<uint32_t>(core_type) + static_cast<uint32_t>(action_type);
        return register_io->Read32(offset) |
               (static_cast<uint64_t>(register_io->Read32(offset + 4)) << 32);
    }

    static void WriteState(RegisterIo* register_io, enum CoreType core_type,
                           enum ActionType action_type, uint64_t value)
    {
        uint32_t offset = static_cast<uint32_t>(core_type) + static_cast<uint32_t>(action_type);
        uint32_t value_low = value & 0xffffffff;
        uint32_t value_high = (value >> 32) & 0xffffffff;
        if (value_low)
            register_io->Write32(offset, value_low);
        if (value_high)
            register_io->Write32(offset + 4, value_high);
    }
};

} // namespace registers

#endif // REGISTERS_H
