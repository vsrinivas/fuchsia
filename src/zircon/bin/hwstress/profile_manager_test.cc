// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "profile_manager.h"

#include <fuchsia/scheduler/cpp/fidl.h>
#include <lib/zx/handle.h>
#include <lib/zx/result.h>
#include <zircon/syscalls.h>

#include <future>
#include <thread>

#include <gtest/gtest.h>

#include "src/lib/testing/predicates/status.h"
#include "testing_util.h"

namespace hwstress {
namespace {

TEST(ProfileManager, ApplyProfiles) {
  std::unique_ptr<ProfileManager> manager = ProfileManager::CreateFromEnvironment();
  ASSERT_TRUE(manager != nullptr);

  // Create a child thread that just blocks on a future.
  std::promise<bool> should_wake;
  auto worker =
      std::make_unique<std::thread>([wake = should_wake.get_future()]() mutable { wake.get(); });

  // Set thread priority.
  EXPECT_OK(manager->SetThreadPriority(worker.get(), /*priority=*/1));

  // Set thread affinity.
  EXPECT_OK(manager->SetThreadAffinity(worker.get(), /*mask=*/1));

  // Ensure our affinity has been set correctly. (The kernel doesn't provide priority
  // information.)
  zx_info_thread info;
  ASSERT_OK(HandleFromThread(worker.get())
                ->get_info(ZX_INFO_THREAD, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(info.cpu_affinity_mask.mask[0], 0x1ul);

  // Clean up our child thread.
  should_wake.set_value(true);
  worker->join();
}

struct FakeProfileProvider : public fuchsia::scheduler::ProfileProvider {
  FakeProfileProvider() = default;

  void GetProfile(uint32_t priority, std::string name, GetProfileCallback callback) override {
    ASSERT_TRUE(!get_profile_called);
    get_profile_called = true;
    requested_priority = priority;
    callback(ZX_OK, zx::profile(0));
  }

  void GetCpuAffinityProfile(fuchsia::scheduler::CpuSet cpu_mask,
                             GetCpuAffinityProfileCallback callback) override {
    get_affinity_profile_called = true;
    requested_mask = cpu_mask;
    callback(ZX_OK, zx::profile(0));
  }

  void GetDeadlineProfile(uint64_t capacity, uint64_t deadline, uint64_t period, std::string name,
                          GetDeadlineProfileCallback callback) override {
    ZX_PANIC("unexpected call");
  }

  void SetProfileByRole(zx::thread thread, std::string role,
                        SetProfileByRoleCallback callback) override {
    ZX_PANIC("unexpected call");
  }

  bool get_affinity_profile_called = false;
  bool get_profile_called = false;
  uint32_t requested_priority = -1;
  fuchsia::scheduler::CpuSet requested_mask{};
};

TEST(ProfileManager, ProfileProviderCalled) {
  testing::LoopbackConnectionFactory factory;

  // Create a connection to a FakeProfileProvider.
  FakeProfileProvider provider;
  ProfileManager manager(factory.CreateSyncPtrTo<fuchsia::scheduler::ProfileProvider>(&provider));

  // Create a child thread that just blocks on a future.
  std::promise<bool> should_wake;
  auto worker =
      std::make_unique<std::thread>([wake = should_wake.get_future()]() mutable { wake.get(); });

  // Set thread priority. The fake gives us an invalid handle, so ignore the error.
  (void)manager.SetThreadPriority(worker.get(), /*priority=*/13);
  EXPECT_TRUE(provider.get_profile_called);
  EXPECT_EQ(provider.requested_priority, 13u);

  // Set thread affinity. The fake gives us an invalid handle, so ignore the error.
  (void)manager.SetThreadAffinity(worker.get(), /*mask=*/0xaa55);
  EXPECT_TRUE(provider.get_affinity_profile_called);
  EXPECT_EQ(provider.requested_mask.mask[0], 0xaa55ul);

  // Clean up our child thread.
  should_wake.set_value(true);
  worker->join();
}

}  // namespace
}  // namespace hwstress
