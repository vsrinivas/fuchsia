// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/graphics/drivers/msd-vsi-vip/src/command_buffer.h"
#include "test_command_buffer.h"

class TestSuspend : public TestCommandBuffer {};

// Tests submitting a simple batch.
TEST_F(TestSuspend, SubmitBatchCheckSuspend) {
  if (!device_->IsSuspendSupported()) {
    GTEST_SKIP();
  }

  device_->StartDeviceThread();

  ASSERT_EQ(device_->power_state(), MsdVsiDevice::PowerState::kSuspended);

  BufferDesc buffer_desc = {
      .buffer_size = 4096,
      .map_page_count = 1,
      .data_size = 4,
      .batch_offset = 0,
      .gpu_addr = 0x10000,
  };
  ASSERT_NO_FATAL_FAILURE(CreateAndSubmitBufferWaitCompletion(default_context(), buffer_desc));

  ASSERT_EQ(device_->power_state(), MsdVsiDevice::PowerState::kOn);

  // device thread waits 10ms before suspending
  while (device_->power_state() != MsdVsiDevice::PowerState::kSuspended) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  ASSERT_EQ(device_->power_state(), MsdVsiDevice::PowerState::kSuspended);
}
