// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DRIVERS_MSD_ARM_MALI_TESTS_UNIT_TESTS_FAKE_CONNECTION_OWNER_BASE_H_
#define SRC_GRAPHICS_DRIVERS_MSD_ARM_MALI_TESTS_UNIT_TESTS_FAKE_CONNECTION_OWNER_BASE_H_

#include "msd_arm_connection.h"

class FakeConnectionOwnerBase : public MsdArmConnection::Owner {
 public:
  ArmMaliCacheCoherencyStatus cache_coherency_status() override {
    return kArmMaliCacheCoherencyNone;
  }
  bool IsProtectedModeSupported() override { return false; }
  void DeregisterConnection() override {}
  void SetCurrentThreadToDefaultPriority() override {}
  PerformanceCounters* performance_counters() override { return nullptr; }
  std::shared_ptr<DeviceRequest::Reply> RunTaskOnDeviceThread(FitCallbackTask task) override {
    // This implementation runs the callback immediately.
    auto real_task = std::make_unique<DeviceRequest>();
    auto reply = real_task->GetReply();
    reply->Signal(task(nullptr));
    return reply;
  }
  std::thread::id GetDeviceThreadId() override { return std::this_thread::get_id(); }
  MagmaMemoryPressureLevel GetCurrentMemoryPressureLevel() override {
    // Only for testing.
    return MAGMA_MEMORY_PRESSURE_LEVEL_NORMAL;
  }
};

#endif  // SRC_GRAPHICS_DRIVERS_MSD_ARM_MALI_TESTS_UNIT_TESTS_FAKE_CONNECTION_OWNER_BASE_H_
