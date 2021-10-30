// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/rcu-manager.h"

#include <lib/async-testing/test_loop.h>
#include <lib/sync/completion.h>
#include <zircon/time.h>

#include <memory>
#include <thread>

#include <zxtest/zxtest.h>

namespace wlan::testing {
namespace {

TEST(RcuManagerTest, NestedReadLock) {
  auto test_loop = std::make_unique<::async::TestLoop>();
  auto manager = std::make_unique<::wlan::iwlwifi::RcuManager>(test_loop->dispatcher());

  manager->InitForThread();
  manager->ReadLock();

  // Call RcuManager::Sync() on another thread.
  bool synced = false;
  std::thread sync_thread([&]() {
    manager->InitForThread();
    manager->Sync();
    synced = true;
  });
  EXPECT_FALSE(synced);

  // Nesting a lock and then unlocking it (without unlocking the outer lock) should not cause the
  // Sync() call to complete yet.
  manager->ReadLock();
  EXPECT_FALSE(synced);
  manager->ReadUnlock();
  EXPECT_FALSE(synced);

  // Unlocking the lock a final time should allow the Sync() call to finish.
  manager->ReadUnlock();
  sync_thread.join();
  EXPECT_TRUE(synced);
}

TEST(RcuManagerTest, ThreadedReadLock) {
  auto test_loop = std::make_unique<::async::TestLoop>();
  auto manager = std::make_unique<::wlan::iwlwifi::RcuManager>(test_loop->dispatcher());

  manager->InitForThread();
  manager->ReadLock();

  // Perform a read-side lock on another thread.
  sync_completion_t thread_started = {};
  sync_completion_t thread_continue = {};
  std::thread lock_thread([&]() {
    manager->InitForThread();
    manager->ReadLock();
    sync_completion_signal(&thread_started);
    sync_completion_wait(&thread_continue, ZX_TIME_INFINITE);
    manager->ReadUnlock();
  });

  // Wait for the other thread to read-side lock.
  sync_completion_wait(&thread_started, ZX_TIME_INFINITE);

  // Start a thread that waits for a sync.
  bool synced = false;
  std::thread sync_thread([&]() {
    manager->InitForThread();
    manager->Sync();
    synced = true;
  });
  EXPECT_FALSE(synced);

  // Unlocking this thread still means another thread is holding the read-side lock.
  manager->ReadUnlock();
  EXPECT_FALSE(synced);

  // Unlocking the other thread finally allows the Sync() to complete.
  sync_completion_signal(&thread_continue);
  lock_thread.join();
  sync_thread.join();
  EXPECT_TRUE(synced);
}

TEST(RcuManagerTest, CallSync) {
  auto test_loop = std::make_unique<::async::TestLoop>();
  auto manager = std::make_unique<::wlan::iwlwifi::RcuManager>(test_loop->dispatcher());

  manager->InitForThread();
  manager->ReadLock();

  // Prepare a call that will execute after all read-side locks are unlocked.
  bool called = false;
  manager->CallSync([](void* data) { *reinterpret_cast<bool*>(data) = true; }, &called);
  std::thread loop_thread([&]() { test_loop->RunUntilIdle(); });
  EXPECT_FALSE(called);

  // A nested lock will not cause the call to executed.
  manager->ReadLock();
  EXPECT_FALSE(called);
  manager->ReadUnlock();
  EXPECT_FALSE(called);

  // Unlocking the last lock allows the call to proceed.
  manager->ReadUnlock();
  loop_thread.join();
  EXPECT_TRUE(called);
}

}  // namespace
}  // namespace wlan::testing
