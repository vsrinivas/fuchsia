// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGISTERS_H
#define REGISTERS_H

#include "magma_util/macros.h"
#include "magma_util/register_bitfields.h"
#include "magma_util/register_io.h"

namespace registers {

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

} // namespace registers

#endif // REGISTERS_H
