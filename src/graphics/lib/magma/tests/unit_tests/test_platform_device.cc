// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "helper/platform_device_helper.h"
#include "platform_device.h"
#include "platform_thread.h"

using std::chrono_literals::operator""ms;

TEST(PlatformDevice, Basic) {
  magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
  ASSERT_TRUE(platform_device);

  auto platform_mmio =
      platform_device->CpuMapMmio(0, magma::PlatformMmio::CACHE_POLICY_UNCACHED_DEVICE);
  EXPECT_TRUE(platform_mmio.get());
}

TEST(PlatformDevice, MapMmio) {
  magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
  ASSERT_TRUE(platform_device);

  uint32_t index = 0;

  // Map once
  auto mmio = platform_device->CpuMapMmio(index, magma::PlatformMmio::CACHE_POLICY_CACHED);
  ASSERT_TRUE(mmio);
  EXPECT_NE(0u, mmio->physical_address());

  // Map again same policy
  auto mmio2 = platform_device->CpuMapMmio(index, magma::PlatformMmio::CACHE_POLICY_CACHED);
  EXPECT_TRUE(mmio2);

  // Map again different policy - this is now permitted though it's a bad idea.
  auto mmio3 = platform_device->CpuMapMmio(index, magma::PlatformMmio::CACHE_POLICY_UNCACHED);
  EXPECT_TRUE(mmio3);
}

TEST(PlatformDevice, SchedulerProfile) {
  magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
  ASSERT_TRUE(platform_device);

  auto profile = platform_device->GetSchedulerProfile(magma::PlatformDevice::kPriorityHigher,
                                                      "msd/test-profile");
  ASSERT_TRUE(profile);

  std::thread test_thread(
      [&profile]() { EXPECT_TRUE(magma::PlatformThreadHelper::SetProfile(profile.get())); });

  test_thread.join();
}

TEST(PlatformDevice, DeadlineSchedulerProfile) {
  magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
  ASSERT_TRUE(platform_device);

  const std::chrono::nanoseconds capacity_ns = 5ms;
  const std::chrono::nanoseconds deadline_ns = 10ms;
  const std::chrono::nanoseconds period_ns = deadline_ns;
  auto profile = platform_device->GetDeadlineSchedulerProfile(capacity_ns, deadline_ns, period_ns,
                                                              "msd/test-profile");
  ASSERT_TRUE(profile);

  std::thread test_thread(
      [&profile]() { EXPECT_TRUE(magma::PlatformThreadHelper::SetProfile(profile.get())); });

  test_thread.join();
}

#ifndef __STDC_NO_THREADS__
static int thread_function(void* input) {
  auto mutex = reinterpret_cast<std::mutex*>(input);
  std::unique_lock<std::mutex> lock(*mutex);
  return 0;
}

TEST(PlatformDevice, SchedulerThreadProfile) {
  magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
  ASSERT_TRUE(platform_device);

  auto profile = platform_device->GetSchedulerProfile(magma::PlatformDevice::kPriorityHigher,
                                                      "msd/test-profile");
  ASSERT_TRUE(profile);

  // Block the thread to prevent it from exiting before we set the profile
  std::mutex mutex;
  std::unique_lock<std::mutex> lock(mutex);

  thrd_t thread;
  ASSERT_EQ(0, thrd_create(&thread, &thread_function, &mutex));

  EXPECT_TRUE(magma::PlatformThreadHelper::SetThreadProfile(thread, profile.get()));

  lock.unlock();
  thrd_join(thread, nullptr);
}
#endif

TEST(PlatformDevice, FirmwareLoader) {
  magma::PlatformDevice* platform_device = TestPlatformDevice::GetInstance();
  ASSERT_TRUE(platform_device);

  std::unique_ptr<magma::PlatformBuffer> buffer;
  uint64_t size;
  EXPECT_EQ(MAGMA_STATUS_OK,
            platform_device->LoadFirmware("test_firmware.txt", &buffer, &size).get());
  EXPECT_NE(nullptr, buffer.get());
  EXPECT_EQ(59u, size);
}
