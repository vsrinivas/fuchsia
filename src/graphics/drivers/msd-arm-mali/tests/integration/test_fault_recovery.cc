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

#include "helper/magma_map_cpu.h"
#include "helper/test_device_helper.h"
#include "magma/magma.h"
#include "magma_arm_mali_types.h"
#include "magma_util/dlog.h"
#include "magma_util/macros.h"
#include "mali_utils.h"
#include "src/graphics/drivers/msd-arm-mali/include/magma_vendor_queries.h"

namespace {

class TestConnection : public magma::TestDeviceBase {
 public:
  TestConnection() : magma::TestDeviceBase(MAGMA_VENDOR_ID_MALI) {
    magma_create_connection2(device(), &connection_);
    DASSERT(connection_);

    magma_create_context(connection_, &context_id_);
    helper_.emplace(connection_, context_id_);
  }

  ~TestConnection() {
    magma_release_context(connection_, context_id_);

    if (connection_)
      magma_release_connection(connection_);
  }

  bool SupportsProtectedMode() {
    uint64_t value_out;
    EXPECT_EQ(MAGMA_STATUS_OK,
              magma_query(device(), kMsdArmVendorQuerySupportsProtectedMode, nullptr, &value_out));
    return !!value_out;
  }

  void SubmitCommandBuffer(mali_utils::AtomHelper::How how, uint8_t atom_number,
                           uint8_t atom_dependency, bool protected_mode) {
    helper_->SubmitCommandBuffer(how, atom_number, atom_dependency, protected_mode);
  }

 private:
  magma_connection_t connection_;
  uint32_t context_id_;
  std::optional<mali_utils::AtomHelper> helper_;
};

}  // namespace

TEST(FaultRecovery, Test) {
  std::unique_ptr<TestConnection> test;
  test.reset(new TestConnection());
  test->SubmitCommandBuffer(mali_utils::AtomHelper::NORMAL, 1, 0, false);
  test.reset(new TestConnection());
  test->SubmitCommandBuffer(mali_utils::AtomHelper::JOB_FAULT, 1, 0, false);
  test.reset(new TestConnection());
  test->SubmitCommandBuffer(mali_utils::AtomHelper::NORMAL, 1, 0, false);
}

TEST(FaultRecovery, TestOrderDependency) {
  std::unique_ptr<TestConnection> test;
  test.reset(new TestConnection());
  test->SubmitCommandBuffer(mali_utils::AtomHelper::NORMAL, 1, 0, false);
  test->SubmitCommandBuffer(mali_utils::AtomHelper::JOB_FAULT, 2, 1, false);
  test->SubmitCommandBuffer(mali_utils::AtomHelper::NORMAL_ORDER, 3, 2, false);
}

TEST(FaultRecovery, TestDataDependency) {
  std::unique_ptr<TestConnection> test;
  test.reset(new TestConnection());
  test->SubmitCommandBuffer(mali_utils::AtomHelper::NORMAL, 1, 0, false);
  test->SubmitCommandBuffer(mali_utils::AtomHelper::JOB_FAULT, 2, 1, false);
  test->SubmitCommandBuffer(mali_utils::AtomHelper::NORMAL_DATA, 3, 2, false);
}

TEST(FaultRecovery, TestMmu) {
  std::unique_ptr<TestConnection> test;
  test.reset(new TestConnection());
  test->SubmitCommandBuffer(mali_utils::AtomHelper::NORMAL, 1, 0, false);
  test.reset(new TestConnection());
  test->SubmitCommandBuffer(mali_utils::AtomHelper::MMU_FAULT, 1, 0, false);
  test.reset(new TestConnection());
  test->SubmitCommandBuffer(mali_utils::AtomHelper::NORMAL, 1, 0, false);
}

TEST(FaultRecovery, TestProtected) {
  std::unique_ptr<TestConnection> test;
  test.reset(new TestConnection());
  if (!test->SupportsProtectedMode()) {
    GTEST_SKIP();
  }
  test->SubmitCommandBuffer(mali_utils::AtomHelper::NORMAL, 1, 0, false);
  test.reset(new TestConnection());
  test->SubmitCommandBuffer(mali_utils::AtomHelper::NORMAL, 1, 0, true);
  test.reset(new TestConnection());
  test->SubmitCommandBuffer(mali_utils::AtomHelper::MMU_FAULT, 1, 0, true);
  test.reset(new TestConnection());
  test->SubmitCommandBuffer(mali_utils::AtomHelper::NORMAL, 1, 0, false);
}
