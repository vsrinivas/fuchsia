// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_FEATURES_H_
#define GPU_FEATURES_H_

#include "magma_util/register_bitfields.h"
#include "magma_util/register_io.h"
#include "registers.h"

struct GpuFeatures {
    static constexpr uint32_t kSuspendSizeOffset = 0x8;
    static constexpr uint32_t kAsPresentOffset = 0x18;
    static constexpr uint32_t kJsPresentOffset = 0x1c;
    static constexpr uint32_t kThreadMaxThreadsOffset = 0xa0;
    static constexpr uint32_t kThreadMaxWorkgroupSizeOffset = 0xa4;
    static constexpr uint32_t kThreadMaxBarrierSizeOffset = 0xa8;
    static constexpr uint32_t kJsFeaturesOffset = 0xc0;
    static constexpr uint32_t kTextureFeaturesOffset = 0xb0;

    // Shader core present bitmap.
    static constexpr uint32_t kShaderPresentLowOffset = 0x100;
    static constexpr uint32_t kShaderPresentHighOffset = 0x104;

    // Tiler present bitmap.
    static constexpr uint32_t kTilerPresentLowOffset = 0x110;
    static constexpr uint32_t kTilerPresentHighOffset = 0x114;

    // L2 cache present bitmap.
    static constexpr uint32_t kL2PresentLowOffset = 0x120;
    static constexpr uint32_t kL2PresentHighOffset = 0x124;

    // Core stack present bitmap.
    static constexpr uint32_t kStackPresentLowOffset = 0xe00;
    static constexpr uint32_t kStackPresentHighOffset = 0xe04;

    static constexpr uint32_t kMaxJobSlots = 16;
    static constexpr uint32_t kNumTextureFeaturesRegisters = 3;

    registers::GpuId gpu_id;
    registers::L2Features l2_features;
    uint32_t suspend_size;
    registers::TilerFeatures tiler_features;
    registers::MemoryFeatures mem_features;
    registers::MmuFeatures mmu_features;
    uint32_t address_space_present;
    uint32_t job_slot_present;
    registers::ThreadFeatures thread_features;
    uint32_t thread_max_threads;
    uint32_t thread_max_workgroup_size;
    uint32_t thread_max_barrier_size;

    uint32_t job_slot_features[kMaxJobSlots];
    uint32_t texture_features[kNumTextureFeaturesRegisters];

    uint64_t shader_present;
    uint64_t tiler_present;
    uint64_t l2_present;
    uint64_t stack_present;

    void ReadFrom(RegisterIo* io)
    {
        gpu_id = registers::GpuId::Get().ReadFrom(io);
        l2_features = registers::L2Features::Get().ReadFrom(io);
        tiler_features = registers::TilerFeatures::Get().ReadFrom(io);
        suspend_size = io->Read32(kSuspendSizeOffset);
        mem_features = registers::MemoryFeatures::Get().ReadFrom(io);
        mmu_features = registers::MmuFeatures::Get().ReadFrom(io);
        address_space_present = io->Read32(kAsPresentOffset);
        job_slot_present = io->Read32(kJsPresentOffset);
        thread_max_threads = io->Read32(kThreadMaxThreadsOffset);
        thread_max_workgroup_size = io->Read32(kThreadMaxWorkgroupSizeOffset);
        thread_max_barrier_size = io->Read32(kThreadMaxBarrierSizeOffset);
        thread_features = registers::ThreadFeatures::Get().ReadFrom(io);

        for (size_t i = 0; i < kMaxJobSlots; i++)
            job_slot_features[i] = io->Read32(kJsFeaturesOffset + i * 4);

        for (size_t i = 0; i < kNumTextureFeaturesRegisters; i++)
            texture_features[i] = io->Read32(kTextureFeaturesOffset + i * 4);

        shader_present = ReadPair(io, kShaderPresentLowOffset);
        tiler_present = ReadPair(io, kTilerPresentLowOffset);
        l2_present = ReadPair(io, kL2PresentLowOffset);
        stack_present = ReadPair(io, kStackPresentLowOffset);
    }

private:
    uint64_t ReadPair(RegisterIo* io, uint32_t low_offset)
    {
        uint64_t low_word = io->Read32(low_offset);
        uint64_t high_word = io->Read32(low_offset + 4);
        return (high_word << 32) | low_word;
    }
};

#endif // GPU_FEATURES_H_
