// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MAGMA_ARM_MALI_TYPES_H_
#define MAGMA_ARM_MALI_TYPES_H_

#include <cstdint>

#include "magma_common_defs.h"

// These flags can be specified to magma_map_buffer_gpu.
enum MagmaArmMaliGpuMapFlags {
    // Accesses to this data should be GPU-L2 coherent.
    kMagmaArmMaliGpuMapFlagInnerShareable = (1 << MAGMA_GPU_MAP_FLAG_VENDOR_SHIFT),
};

enum AtomCoreRequirements {
    kAtomCoreRequirementFragmentShader = (1 << 0),

    // Compute shaders also include vertex and geometry shaders.
    kAtomCoreRequirementComputeShader = (1 << 1),
    kAtomCoreRequirementTiler = (1 << 2),
};

enum ArmMaliResultCode { kArmMaliResultSuccess = 1 };

// This is arbitrary user data that's used to identify an atom.
struct magma_arm_mali_user_data {
    uint64_t data[2];
} __attribute__((packed));

struct magma_arm_mali_atom {
    uint64_t job_chain_addr;
    struct magma_arm_mali_user_data data;
    uint32_t core_requirements; // a set of AtomCoreRequirements.
    uint8_t atom_number;
} __attribute__((packed));

struct magma_arm_mali_status {
    uint32_t result_code;
    uint8_t atom_number;
    magma_arm_mali_user_data data;
} __attribute__((packed));

#endif
