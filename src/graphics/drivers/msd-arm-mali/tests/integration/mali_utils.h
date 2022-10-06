// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_GRAPHICS_DRIVERS_MSD_ARM_MALI_TESTS_INTEGRATION_MALI_UTILS_H_
#define SRC_GRAPHICS_DRIVERS_MSD_ARM_MALI_TESTS_INTEGRATION_MALI_UTILS_H_

#include "helper/magma_map_cpu.h"
#include "helper/test_device_helper.h"
#include "magma/magma.h"
#include "magma_arm_mali_types.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "src/graphics/drivers/msd-arm-mali/include/magma_vendor_queries.h"

namespace mali_utils {

enum JobDescriptorType { kJobDescriptorTypeNop = 1 };

struct JobDescriptorHeader {
  uint64_t reserved1;
  uint64_t reserved2;
  uint8_t job_descriptor_size : 1;
  uint8_t job_type : 7;
  uint8_t reserved3;
  uint16_t reserved4;
  uint16_t reserved5;
  uint16_t reserved6;
  uint64_t next_job;
};

class AtomHelper {
 public:
  enum class JobBufferType {
    kValid,
    kInvalid,
  };

  enum class AtomDepType {
    kOrder,
    kData,
  };

  enum How { NORMAL, NORMAL_ORDER, NORMAL_DATA, JOB_FAULT, MMU_FAULT };

  AtomHelper(magma_connection_t connection, uint32_t context_id)
      : connection_(connection), context_id_(context_id) {}

  bool InitJobBuffer(magma_buffer_t buffer, JobBufferType type, uint64_t size, uint64_t* job_va);
  bool InitAtomDescriptor(void* vaddr, uint64_t size, uint64_t job_va, uint8_t atom_number,
                          uint8_t atom_dependency, AtomDepType dep_type, bool use_invalid_address,
                          bool protected_mode);

  void SubmitCommandBuffer(How how, uint8_t atom_number, uint8_t atom_dependency,
                           bool protected_mode);

 private:
  magma_connection_t connection_;
  uint32_t context_id_;
  // Arbitrary page-aligned value. Must be > 0 and < 2**33 (for 33-bit VA devices).
  uint64_t next_job_address_ = 0x1000000;
};

}  // namespace mali_utils

#endif  // SRC_GRAPHICS_DRIVERS_MSD_ARM_MALI_TESTS_INTEGRATION_MALI_UTILS_H_
