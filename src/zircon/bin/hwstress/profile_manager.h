// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_ZIRCON_BIN_HWSTRESS_PROFILE_MANAGER_H_
#define SRC_ZIRCON_BIN_HWSTRESS_PROFILE_MANAGER_H_

#include <fuchsia/scheduler/cpp/fidl.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/profile.h>
#include <lib/zx/status.h>
#include <lib/zx/thread.h>
#include <zircon/compiler.h>
#include <zircon/status.h>

#include <memory>
#include <unordered_map>
#include <utility>

namespace hwstress {

// A ProfileManager creates, caches, and applies Zircon scheduling profilings to
// threads.
//
// Thread safe.
class ProfileManager {
 public:
  // Create a new profile manager from services in the environment.
  static std::unique_ptr<ProfileManager> CreateFromEnvironment();

  // Create a new profile manager.
  explicit ProfileManager(fuchsia::scheduler::ProfileProviderSyncPtr profile_provider);

  // Apply a given affinity mask to the given thread.
  //
  // Bit |i| in the mask being set corresponds to the thread being allowed to run on CPU |i|.
  zx_status_t SetThreadAffinity(std::thread* thread, uint32_t mask);
  zx_status_t SetThreadAffinity(const zx::thread& thread, uint32_t mask);

  // Apply a given priority mask to the given thread.
  zx_status_t SetThreadPriority(std::thread* thread, uint32_t priority);
  zx_status_t SetThreadPriority(const zx::thread& thread, uint32_t priority);

 private:
  // Disallow copy and move.
  ProfileManager(const ProfileManager&) = delete;
  ProfileManager& operator=(const ProfileManager&) = delete;

  // Apply a profile to the given thread.
  //
  // If the profile already exists in |cache|, we apply that. Otherwise we create a
  // new profile using |create_fn|.
  template <typename T>
  zx_status_t CreateAndApplyProfile(std::unordered_map<T, zx::profile>* cache, T key,
                                    std::function<zx::result<zx::profile>(T)> create_fn,
                                    const zx::thread& thread);

  fuchsia::scheduler::ProfileProviderSyncPtr profile_provider_;
  std::mutex mutex_;
  std::unordered_map<uint32_t, zx::profile> affinity_profiles_ __TA_GUARDED(mutex_);
  std::unordered_map<uint32_t, zx::profile> priority_profiles_ __TA_GUARDED(mutex_);
};

// Return the Zircon handle from a given C++ thread.
zx::unowned<zx::thread> HandleFromThread(std::thread* thread);

//
// Implementation details.
//

template <typename T>
zx_status_t ProfileManager::CreateAndApplyProfile(
    std::unordered_map<T, zx::profile>* cache, T key,
    std::function<zx::result<zx::profile>(T)> create_fn, const zx::thread& thread) {
  std::lock_guard<std::mutex> guard(mutex_);

  // If we already have a profile for this priority, just use that.
  auto it = cache->find(key);
  if (it != cache->end()) {
    return thread.set_profile(it->second, /*options=*/0);
  }

  // Create a profile.
  zx::result<zx::profile> profile = create_fn(key);
  if (profile.is_error()) {
    return profile.error_value();
  }

  // Save it and apply it.
  zx_status_t status = thread.set_profile(profile.value(), /*options=*/0);
  (*cache)[key] = std::move(profile.value());
  return status;
}

}  // namespace hwstress

#endif  // SRC_ZIRCON_BIN_HWSTRESS_PROFILE_MANAGER_H_
