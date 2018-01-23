// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGISTERS_H
#define REGISTERS_H

#include "magma_util/macros.h"
#include "magma_util/register_bitfields.h"
#include "magma_util/register_io.h"

namespace registers {

class GpuId : public magma::RegisterBase {
public:
    DEF_FIELD(3, 0, version_status);
    DEF_FIELD(11, 4, minor_revision);
    DEF_FIELD(15, 12, major_revision);
    DEF_FIELD(31, 16, product_id);

    static auto Get() { return magma::RegisterAddr<GpuId>(0); }
};

class L2Features : public magma::RegisterBase {
public:
    DEF_FIELD(7, 0, log2_line_size);
    DEF_FIELD(15, 8, associativity);
    DEF_FIELD(23, 16, log2_cache_size);
    DEF_FIELD(31, 24, external_bus_width);

    static auto Get() { return magma::RegisterAddr<L2Features>(0x4); }
};

class TilerFeatures : public magma::RegisterBase {
public:
    DEF_FIELD(5, 0, log2_bin_size_bytes);
    DEF_FIELD(11, 8, max_active_levels);

    static auto Get() { return magma::RegisterAddr<TilerFeatures>(0xc); }
};

class MemoryFeatures : public magma::RegisterBase {
public:
    DEF_FIELD(1, 0, groups_l2_coherent);
    DEF_FIELD(11, 8, num_l2_slices_minus1);

    static auto Get() { return magma::RegisterAddr<MemoryFeatures>(0x10); }
};

class MmuFeatures : public magma::RegisterBase {
public:
    DEF_FIELD(7, 0, va_bits);
    DEF_FIELD(15, 8, pa_bits);

    static auto Get() { return magma::RegisterAddr<MmuFeatures>(0x14); }
};

class ThreadFeatures : public magma::RegisterBase {
public:
    DEF_FIELD(15, 0, max_registers);
    DEF_FIELD(23, 16, max_task_queue);
    DEF_FIELD(29, 24, max_thread_group_split);
    DEF_FIELD(31, 30, impl_tech);

    static auto Get() { return magma::RegisterAddr<ThreadFeatures>(0xac); }
};

class CoherencyFeatures : public magma::RegisterBase {
public:
    // ACE-lite lets the GPU snoop on changes made by the CPU
    DEF_BIT(0, ace_lite);

    // ACE lets the GPU and CPU snoop on each other.
    DEF_BIT(1, ace);

    DEF_BIT(31, none);

    static auto GetPresent() { return magma::RegisterAddr<CoherencyFeatures>(0x300); }
    static auto GetEnable() { return magma::RegisterAddr<CoherencyFeatures>(0x304); }
};

class GpuStatus : public magma::RegisterBase {
public:
    DEF_BIT(0, gpu_active);
    DEF_BIT(1, power_active);
    DEF_BIT(2, performance_counters_active);

    static auto Get() { return magma::RegisterAddr<GpuStatus>(0x34); }
};

// May return incorrect value on rollover.
class CycleCount : public magma::RegisterPairBase {
public:
    static auto Get() { return magma::RegisterAddr<CycleCount>(0x90); }
};

// May return incorrect value on rollover.
class Timestamp : public magma::RegisterPairBase {
public:
    static auto Get() { return magma::RegisterAddr<Timestamp>(0x98); }
};

class GpuCommand {
public:
    static constexpr uint32_t kOffset = 0x30;

    enum {
        kCmdNop = 0,
        kCmdSoftReset = 0x1,
        kCmdHardReset = 0x2,
        kCmdClearPerformanceCounters = 0x3,
        kCmdSamplePerformanceCounters = 0x4,
        kCmdCycleCountStart = 0x5,
        kCmdCycleCountStop = 0x6,
        kCmdCleanCaches = 0x7,
        kCmdCleanAndInvalidateCaches = 0x8,
        kCmdSetProtectedMode = 0x9,
    };
};

class GpuIrqFlags : public magma::RegisterBase {
public:
    DEF_BIT(0, gpu_fault);
    DEF_BIT(7, multiple_gpu_faults);
    DEF_BIT(8, reset_completed);
    DEF_BIT(9, power_changed_single);
    DEF_BIT(10, power_changed_all);
    DEF_BIT(16, performance_counter_sample_completed);
    DEF_BIT(17, clean_caches_completed);

    static auto GetRawStat() { return magma::RegisterAddr<GpuIrqFlags>(0x20); }
    static auto GetIrqClear() { return magma::RegisterAddr<GpuIrqFlags>(0x24); }
    static auto GetIrqMask() { return magma::RegisterAddr<GpuIrqFlags>(0x28); }
    static auto GetStatus() { return magma::RegisterAddr<GpuIrqFlags>(0x2c); }
};

class MmuIrqFlags : public magma::RegisterBase {
public:
    DEF_FIELD(15, 0, pf_flags);
    DEF_FIELD(31, 16, bf_flags);

    static auto GetRawStat() { return magma::RegisterAddr<MmuIrqFlags>(0x2000); }
    static auto GetIrqClear() { return magma::RegisterAddr<MmuIrqFlags>(0x2004); }
    static auto GetIrqMask() { return magma::RegisterAddr<MmuIrqFlags>(0x2008); }
    static auto GetStatus() { return magma::RegisterAddr<MmuIrqFlags>(0x200c); }
};

class JobIrqFlags : public magma::RegisterBase {
public:
    DEF_FIELD(15, 0, finished_slots);
    DEF_FIELD(31, 16, failed_slots);

    static auto GetRawStat() { return magma::RegisterAddr<JobIrqFlags>(0x1000); }
    static auto GetIrqClear() { return magma::RegisterAddr<JobIrqFlags>(0x1004); }
    static auto GetIrqMask() { return magma::RegisterAddr<JobIrqFlags>(0x1008); }
    static auto GetStatus() { return magma::RegisterAddr<JobIrqFlags>(0x100c); }
};

// Not legal to write to while the performance counters are enabled.
class PerformanceCounterBase : public magma::RegisterPairBase {
public:
    static auto Get() { return magma::RegisterAddr<PerformanceCounterBase>(0x60); }
};

class PerformanceCounterConfig : public magma::RegisterBase {
public:
    enum Mode {
        kModeDisabled = 0,
        kModeManual = 1,
    };
    DEF_FIELD(3, 0, mode);
    DEF_FIELD(7, 4, address_space);

    static auto Get() { return magma::RegisterAddr<PerformanceCounterConfig>(0x68); }
};

// Not legal to write to while the performance counters are enabled.
class PerformanceCounterJmEnable : public magma::RegisterBase {
public:
    static auto Get() { return magma::RegisterAddr<PerformanceCounterJmEnable>(0x6c); }
};

// Not legal to write to while the performance counters are enabled.
class PerformanceCounterShaderEnable : public magma::RegisterBase {
public:
    static auto Get() { return magma::RegisterAddr<PerformanceCounterShaderEnable>(0x70); }
};

// Not legal to write to while the performance counters are enabled.
class PerformanceCounterTilerEnable : public magma::RegisterBase {
public:
    static auto Get() { return magma::RegisterAddr<PerformanceCounterTilerEnable>(0x74); }
};

// Not legal to write to while the performance counters are enabled.
class PerformanceCounterMmuL2Enable : public magma::RegisterBase {
public:
    static auto Get() { return magma::RegisterAddr<PerformanceCounterMmuL2Enable>(0x7c); }
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
    static uint64_t ReadBitmask(magma::RegisterIo* register_io, CoreType core_type,
                                StatusType action_type)
    {
        DASSERT(core_type != CoreType::kCoreStack);

        uint32_t offset = static_cast<uint32_t>(core_type) + static_cast<uint32_t>(action_type);
        return register_io->Read32(offset) |
               (static_cast<uint64_t>(register_io->Read32(offset + 4)) << 32);
    }

    static void WriteState(magma::RegisterIo* register_io, enum CoreType core_type,
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

class GpuFaultStatus : public magma::RegisterBase {
public:
    static auto Get() { return magma::RegisterAddr<GpuFaultStatus>(0x3c); }
};

class GpuFaultAddress : public magma::RegisterPairBase {
public:
    static auto Get() { return magma::RegisterAddr<GpuFaultAddress>(0x40); }
};

class AsTranslationTable : public magma::RegisterPairBase {
public:
    static constexpr uint32_t kBaseAddr = 0x0;
};

class AsMemoryAttributes : public magma::RegisterPairBase {
public:
    static constexpr uint32_t kBaseAddr = 0x8;
};

class AsLockAddress : public magma::RegisterPairBase {
public:
    static constexpr uint32_t kBaseAddr = 0x10;
};

class AsCommand : public magma::RegisterBase {
public:
    static constexpr uint32_t kBaseAddr = 0x18;

    enum {
        kCmdNop = 0x0,
        kCmdUpdate = 0x1,
        kCmdLock = 0x2,
        kCmdUnlock = 0x3,
        kCmdFlush = 0x4,
        kCmdFlushPageTable = 0x4,
        kCmdFlushMem = 0x5
    };
};

class AsFaultStatus : public magma::RegisterBase {
public:
    static constexpr uint32_t kBaseAddr = 0x1c;
};

class AsFaultAddress : public magma::RegisterPairBase {
public:
    static constexpr uint32_t kBaseAddr = 0x20;
};

class AsStatus : public magma::RegisterBase {
public:
    static constexpr uint32_t kBaseAddr = 0x28;
};

class AsTransConfig : public magma::RegisterPairBase {
public:
    static constexpr uint32_t kBaseAddr = 0x30;
};

class AsFaultExtra : public magma::RegisterPairBase {
public:
    static constexpr uint32_t kBaseAddr = 0x30;
};

class AsRegisters {
public:
    static constexpr uint32_t kBaseAddr = 0x2400;
    static constexpr uint32_t kAsStride = 0x40;

    // Maximum registers.
    static constexpr uint32_t kAddressSpacesCount = 16;

    AsRegisters(uint32_t address_space) : address_space_(address_space)
    {
        DASSERT(address_space < kAddressSpacesCount);
    }

    uint32_t address_space() const { return address_space_; }

    auto TranslationTable() { return GetReg<registers::AsTranslationTable>(); }
    auto MemoryAttributes() { return GetReg<registers::AsMemoryAttributes>(); }
    auto LockAddress() { return GetReg<registers::AsLockAddress>(); }
    auto Command() { return GetReg<registers::AsCommand>(); }
    auto FaultStatus() { return GetReg<registers::AsFaultStatus>(); }
    auto FaultAddress() { return GetReg<registers::AsFaultAddress>(); }
    auto Status() { return GetReg<registers::AsStatus>(); }
    auto TransConfig() { return GetReg<registers::AsTransConfig>(); }
    auto FaultExtra() { return GetReg<registers::AsFaultExtra>(); }

private:
    template <class RegType> magma::RegisterAddr<RegType> GetReg()
    {
        return magma::RegisterAddr<RegType>(RegType::kBaseAddr + kBaseAddr +
                                            kAsStride * address_space_);
    }

    uint32_t address_space_;
};

class JobSlotConfig : public magma::RegisterBase {
public:
    static constexpr uint32_t kBaseAddr = 0x18;
    static constexpr uint32_t kBaseAddrNext = 0x58;

    DEF_FIELD(7, 0, address_space);
    DEF_BIT(8, start_flush_clean);
    DEF_BIT(9, start_flush_invalidate);
    DEF_BIT(10, start_mmu);
    DEF_BIT(11, job_chain_flag);
    DEF_BIT(12, end_flush_clean);
    DEF_BIT(13, end_flush_invalidate);
    DEF_BIT(14, enable_flush_reduction);
    DEF_BIT(15, disable_descriptor_write_back);
    DEF_FIELD(23, 16, thread_priority);
};

class JobSlotHead : public magma::RegisterPairBase {
public:
    static constexpr uint32_t kBaseAddr = 0x00;
    static constexpr uint32_t kBaseAddrNext = 0x40;
};

class JobSlotAffinity : public magma::RegisterPairBase {
public:
    static constexpr uint32_t kBaseAddr = 0x10;
    static constexpr uint32_t kBaseAddrNext = 0x50;
};

class JobSlotXAffinity : public magma::RegisterBase {
public:
    static constexpr uint32_t kBaseAddr = 0x1c;
    static constexpr uint32_t kBaseAddrNext = 0x5c;
};

class JobSlotCommand : public magma::RegisterBase {
public:
    static constexpr uint32_t kCommandNop = 0x0;
    static constexpr uint32_t kCommandStart = 0x1;
    static constexpr uint32_t kCommandSoftStop = 0x2;
    static constexpr uint32_t kCommandHardStop = 0x3;
    static constexpr uint32_t kCommandSoftStop_0 = 0x4;
    static constexpr uint32_t kCommandHardStop_0 = 0x5;
    static constexpr uint32_t kCommandStopStop_1 = 0x6;
    static constexpr uint32_t kCommandHardStop_1 = 0x7;

    static constexpr uint32_t kBaseAddr = 0x20;
    static constexpr uint32_t kBaseAddrNext = 0x60;
};

class JobSlotStatus : public magma::RegisterBase {
public:
    static constexpr uint32_t kBaseAddr = 0x24;
};

class JobSlotTail : public magma::RegisterPairBase {
public:
    static constexpr uint32_t kBaseAddr = 0x08;
};

class JobSlotFlushId : public magma::RegisterBase {
public:
    static constexpr uint32_t kBaseAddrNext = 0x70;
};

class JobSlotRegisters {
public:
    static constexpr uint32_t kBaseAddr = 0x1000 + 0x800;
    static constexpr uint32_t kJobSlotStride = 0x80;

    // Maximum registers - actual hardware provides fewer.
    static constexpr uint32_t kJobSlotsCount = 16;

    JobSlotRegisters(uint32_t job_slot) : job_slot_(job_slot)
    {
        DASSERT(job_slot < kJobSlotsCount);
    }

    // These registers are for the currently executing job.
    auto Head() { return GetReg<registers::JobSlotHead>(); }
    auto Tail() { return GetReg<registers::JobSlotTail>(); }
    auto Status() { return GetReg<registers::JobSlotStatus>(); }
    auto Config() { return GetReg<registers::JobSlotConfig>(); }
    auto Affinity() { return GetReg<registers::JobSlotAffinity>(); }
    auto XAffinity() { return GetReg<registers::JobSlotXAffinity>(); }
    auto Command() { return GetReg<registers::JobSlotCommand>(); }

    // These registers are for the next job to execute. It can start executing
    // once the start command is sent.
    auto HeadNext() { return GetRegNext<registers::JobSlotHead>(); }
    auto ConfigNext() { return GetRegNext<registers::JobSlotConfig>(); }
    auto AffinityNext() { return GetRegNext<registers::JobSlotAffinity>(); }
    auto XAffinityNext() { return GetRegNext<registers::JobSlotXAffinity>(); }
    auto CommandNext() { return GetRegNext<registers::JobSlotCommand>(); }

private:
    template <class RegType> magma::RegisterAddr<RegType> GetRegNext()
    {
        return magma::RegisterAddr<RegType>(RegType::kBaseAddrNext + kBaseAddr +
                                            kJobSlotStride * job_slot_);
    }

    template <class RegType> magma::RegisterAddr<RegType> GetReg()
    {
        return magma::RegisterAddr<RegType>(RegType::kBaseAddr + kBaseAddr +
                                            kJobSlotStride * job_slot_);
    }

    uint32_t job_slot_;
};

} // namespace registers

#endif // REGISTERS_H
