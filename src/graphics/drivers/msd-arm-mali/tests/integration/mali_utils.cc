// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mali_utils.h"

namespace mali_utils {

bool AtomHelper::InitJobBuffer(magma_buffer_t buffer, JobBufferType type, uint64_t size,
                               uint64_t* job_va) {
  void* vaddr;
  if (!magma::MapCpuHelper(buffer, 0 /*offset*/, size, &vaddr) != 0)
    return DRETF(false, "couldn't map job buffer");
  *job_va = next_job_address_;
  next_job_address_ += 0x5000;
  magma_map_buffer(
      connection_, *job_va, buffer, 0, magma::page_size(),
      MAGMA_MAP_FLAG_READ | MAGMA_MAP_FLAG_WRITE | kMagmaArmMaliGpuMapFlagInnerShareable);
  magma_buffer_range_op(connection_, buffer, MAGMA_BUFFER_RANGE_OP_POPULATE_TABLES, 0,
                        magma::page_size());
  JobDescriptorHeader* header = static_cast<JobDescriptorHeader*>(vaddr);
  memset(header, 0, sizeof(*header));
  header->job_descriptor_size = 1;  // Next job address is 64-bit.
  if (type == JobBufferType::kInvalid) {
    header->job_type = 127;
  } else {
    header->job_type = kJobDescriptorTypeNop;
  }
  header->next_job = 0;
  magma_clean_cache(buffer, 0, PAGE_SIZE, MAGMA_CACHE_OPERATION_CLEAN);
  magma::UnmapCpuHelper(vaddr, size);
  return true;
}

bool AtomHelper::InitAtomDescriptor(void* vaddr, uint64_t size, uint64_t job_va,
                                    uint8_t atom_number, uint8_t atom_dependency,
                                    AtomDepType dep_type, bool use_invalid_address,
                                    bool protected_mode) {
  memset(vaddr, 0, size);

  magma_arm_mali_atom* atom = static_cast<magma_arm_mali_atom*>(vaddr);
  atom->size = sizeof(*atom);
  if (use_invalid_address) {
    atom->job_chain_addr = job_va - PAGE_SIZE;
    if (atom->job_chain_addr == 0)
      atom->job_chain_addr = PAGE_SIZE * 2;
  } else {
    atom->job_chain_addr = job_va;
  }
  atom->atom_number = atom_number;
  atom->dependencies[0].atom_number = atom_dependency;
  atom->dependencies[0].type =
      dep_type == AtomDepType::kData ? kArmMaliDependencyData : kArmMaliDependencyOrder;
  if (protected_mode) {
    atom->flags |= kAtomFlagProtected;
  }

  return true;
}
void AtomHelper::SubmitCommandBuffer(How how, uint8_t atom_number, uint8_t atom_dependency,
                                     bool protected_mode) {
  ASSERT_NE(connection_, 0u);

  uint64_t size;
  magma_buffer_t job_buffer;

  ASSERT_EQ(magma_create_buffer(connection_, PAGE_SIZE, &size, &job_buffer), 0);
  uint64_t job_va;
  InitJobBuffer(job_buffer, how == JOB_FAULT ? JobBufferType::kInvalid : JobBufferType::kValid,
                size, &job_va);

  std::vector<uint8_t> vaddr(sizeof(magma_arm_mali_atom));

  ASSERT_TRUE(InitAtomDescriptor(vaddr.data(), vaddr.size(), job_va, atom_number, atom_dependency,
                                 how == NORMAL_DATA ? AtomDepType::kData : AtomDepType::kOrder,
                                 how == MMU_FAULT, protected_mode));

  magma_inline_command_buffer command_buffer;
  command_buffer.data = vaddr.data();
  command_buffer.size = vaddr.size();
  command_buffer.semaphore_ids = nullptr;
  command_buffer.semaphore_count = 0;
  EXPECT_EQ(MAGMA_STATUS_OK,
            magma_execute_immediate_commands2(connection_, context_id_, 1, &command_buffer));

  constexpr uint64_t kOneSecondPerNs = 1000000000;
  magma_poll_item_t item = {.handle = magma_get_notification_channel_handle(connection_),
                            .type = MAGMA_POLL_TYPE_HANDLE,
                            .condition = MAGMA_POLL_CONDITION_READABLE};
  EXPECT_EQ(MAGMA_STATUS_OK, magma_poll(&item, 1, kOneSecondPerNs));

  magma_arm_mali_status status;
  uint64_t status_size;
  magma_bool_t more_data;
  EXPECT_EQ(MAGMA_STATUS_OK, magma_read_notification_channel2(connection_, &status, sizeof(status),
                                                              &status_size, &more_data));
  EXPECT_EQ(status_size, sizeof(status));
  EXPECT_EQ(atom_number, status.atom_number);

  switch (how) {
    case NORMAL:
    case NORMAL_ORDER:
      EXPECT_EQ(kArmMaliResultSuccess, status.result_code);
      break;

    case JOB_FAULT:
    case NORMAL_DATA:
      EXPECT_NE(kArmMaliResultReadFault, status.result_code);
      EXPECT_NE(kArmMaliResultSuccess, status.result_code);
      break;

    case MMU_FAULT:
      if (protected_mode) {
        EXPECT_EQ(kArmMaliResultUnknownFault, status.result_code);
      } else {
        EXPECT_EQ(kArmMaliResultReadFault, status.result_code);
      }
      break;
  }

  magma_release_buffer(connection_, job_buffer);
}
}  // namespace mali_utils
