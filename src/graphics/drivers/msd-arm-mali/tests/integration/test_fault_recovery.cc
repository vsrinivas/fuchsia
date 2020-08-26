// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define MAGMA_DLOG_ENABLE 1

#include <fcntl.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <poll.h>

#include <thread>

#include <gtest/gtest.h>

#include "helper/test_device_helper.h"
#include "magma.h"
#include "magma_arm_mali_types.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "magma_vendor_queries.h"

namespace {

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

class TestConnection : public magma::TestDeviceBase {
 public:
  TestConnection() : magma::TestDeviceBase(MAGMA_VENDOR_ID_MALI) {
    magma_create_connection2(device(), &connection_);
    DASSERT(connection_);

    magma_create_context(connection_, &context_id_);
  }

  ~TestConnection() {
    magma_release_context(connection_, context_id_);

    if (connection_)
      magma_release_connection(connection_);
  }

  bool SupportsProtectedMode() {
    uint64_t value_out;
    EXPECT_EQ(MAGMA_STATUS_OK,
              magma_query2(device(), kMsdArmVendorQuerySupportsProtectedMode, &value_out));
    return !!value_out;
  }

  enum How { NORMAL, NORMAL_ORDER, NORMAL_DATA, JOB_FAULT, MMU_FAULT };

  void SubmitCommandBuffer(How how, uint8_t atom_number, uint8_t atom_dependency,
                           bool protected_mode) {
    ASSERT_NE(connection_, nullptr);

    uint64_t size;
    magma_buffer_t job_buffer;

    ASSERT_EQ(magma_create_buffer(connection_, PAGE_SIZE, &size, &job_buffer), 0);
    uint64_t job_va;
    InitJobBuffer(job_buffer, how, &job_va);

    std::vector<uint8_t> vaddr(sizeof(magma_arm_mali_atom));

    ASSERT_TRUE(InitBatchBuffer(vaddr.data(), vaddr.size(), job_va, atom_number, atom_dependency,
                                how, protected_mode));

    magma_inline_command_buffer command_buffer;
    command_buffer.data = vaddr.data();
    command_buffer.size = vaddr.size();
    command_buffer.semaphore_ids = nullptr;
    command_buffer.semaphore_count = 0;
    magma_execute_immediate_commands2(connection_, context_id_, 1, &command_buffer);

    constexpr uint64_t kOneSecondPerNs = 1000000000;
    EXPECT_EQ(MAGMA_STATUS_OK, magma_wait_notification_channel(connection_, kOneSecondPerNs));

    magma_arm_mali_status status;
    uint64_t status_size;
    EXPECT_EQ(MAGMA_STATUS_OK,
              magma_read_notification_channel(connection_, &status, sizeof(status), &status_size));
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

  bool InitBatchBuffer(void* vaddr, uint64_t size, uint64_t job_va, uint8_t atom_number,
                       uint8_t atom_dependency, How how, bool protected_mode) {
    memset(vaddr, 0, size);

    magma_arm_mali_atom* atom = static_cast<magma_arm_mali_atom*>(vaddr);
    atom->size = sizeof(*atom);
    if (how == MMU_FAULT) {
      atom->job_chain_addr = job_va - PAGE_SIZE;
      if (atom->job_chain_addr == 0)
        atom->job_chain_addr = PAGE_SIZE * 2;
    } else {
      atom->job_chain_addr = job_va;
    }
    atom->atom_number = atom_number;
    atom->dependencies[0].atom_number = atom_dependency;
    atom->dependencies[0].type =
        how == NORMAL_DATA ? kArmMaliDependencyData : kArmMaliDependencyOrder;
    if (protected_mode) {
      atom->flags |= kAtomFlagProtected;
    }

    return true;
  }

  bool InitJobBuffer(magma_buffer_t buffer, How how, uint64_t* job_va) {
    void* vaddr;
    if (magma_map(connection_, buffer, &vaddr) != 0)
      return DRETF(false, "couldn't map job buffer");
    *job_va = next_job_address_;
    next_job_address_ += 0x5000;
    magma_map_buffer_gpu(
        connection_, buffer, 0, 1, *job_va,
        MAGMA_GPU_MAP_FLAG_READ | MAGMA_GPU_MAP_FLAG_WRITE | kMagmaArmMaliGpuMapFlagInnerShareable);
    magma_commit_buffer(connection_, buffer, 0, 1);
    JobDescriptorHeader* header = static_cast<JobDescriptorHeader*>(vaddr);
    memset(header, 0, sizeof(*header));
    header->job_descriptor_size = 1;  // Next job address is 64-bit.
    if (how == JOB_FAULT) {
      header->job_type = 127;
    } else {
      header->job_type = kJobDescriptorTypeNop;
    }
    header->next_job = 0;
    magma_clean_cache(buffer, 0, PAGE_SIZE, MAGMA_CACHE_OPERATION_CLEAN);
    return true;
  }

 private:
  magma_connection_t connection_;
  uint32_t context_id_;
  uint64_t next_job_address_ = 0x1000000;
};

}  // namespace

TEST(FaultRecovery, Test) {
  std::unique_ptr<TestConnection> test;
  test.reset(new TestConnection());
  test->SubmitCommandBuffer(TestConnection::NORMAL, 1, 0, false);
  test.reset(new TestConnection());
  test->SubmitCommandBuffer(TestConnection::JOB_FAULT, 1, 0, false);
  test.reset(new TestConnection());
  test->SubmitCommandBuffer(TestConnection::NORMAL, 1, 0, false);
}

TEST(FaultRecovery, TestOrderDependency) {
  std::unique_ptr<TestConnection> test;
  test.reset(new TestConnection());
  test->SubmitCommandBuffer(TestConnection::NORMAL, 1, 0, false);
  test->SubmitCommandBuffer(TestConnection::JOB_FAULT, 2, 1, false);
  test->SubmitCommandBuffer(TestConnection::NORMAL_ORDER, 3, 2, false);
}

TEST(FaultRecovery, TestDataDependency) {
  std::unique_ptr<TestConnection> test;
  test.reset(new TestConnection());
  test->SubmitCommandBuffer(TestConnection::NORMAL, 1, 0, false);
  test->SubmitCommandBuffer(TestConnection::JOB_FAULT, 2, 1, false);
  test->SubmitCommandBuffer(TestConnection::NORMAL_DATA, 3, 2, false);
}

TEST(FaultRecovery, TestMmu) {
  std::unique_ptr<TestConnection> test;
  test.reset(new TestConnection());
  test->SubmitCommandBuffer(TestConnection::NORMAL, 1, 0, false);
  test.reset(new TestConnection());
  test->SubmitCommandBuffer(TestConnection::MMU_FAULT, 1, 0, false);
  test.reset(new TestConnection());
  test->SubmitCommandBuffer(TestConnection::NORMAL, 1, 0, false);
}

TEST(FaultRecovery, TestProtected) {
  std::unique_ptr<TestConnection> test;
  test.reset(new TestConnection());
  if (!test->SupportsProtectedMode()) {
    printf("Protected mode not supported, skipping\n");
    return;
  }
  test->SubmitCommandBuffer(TestConnection::NORMAL, 1, 0, false);
  test.reset(new TestConnection());
  test->SubmitCommandBuffer(TestConnection::NORMAL, 1, 0, true);
  test.reset(new TestConnection());
  test->SubmitCommandBuffer(TestConnection::MMU_FAULT, 1, 0, true);
  test.reset(new TestConnection());
  test->SubmitCommandBuffer(TestConnection::NORMAL, 1, 0, false);
}
