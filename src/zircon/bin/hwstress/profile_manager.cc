// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "profile_manager.h"

#include <fuchsia/scheduler/cpp/fidl.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/profile.h>
#include <lib/zx/status.h>
#include <threads.h>
#include <zircon/assert.h>
#include <zircon/threads.h>

#include <memory>
#include <thread>
#include <unordered_map>
#include <utility>

#include "src/lib/fxl/strings/string_printf.h"

namespace hwstress {

zx::unowned<zx::thread> HandleFromThread(std::thread* thread) {
  // The native handle is a thrd_t. We don't currently provide any nice way to convert this, other
  // than this ugly cast.
  zx_handle_t handle = thrd_get_zx_handle(static_cast<thrd_t>(thread->native_handle()));
  return zx::unowned<zx::thread>(handle);
}

std::unique_ptr<ProfileManager> ProfileManager::CreateFromEnvironment() {
  std::shared_ptr<sys::ServiceDirectory> svc = sys::ServiceDirectory::CreateFromNamespace();
  if (svc == nullptr) {
    return nullptr;
  }

  fuchsia::scheduler::ProfileProviderSyncPtr profile_provider;
  zx_status_t status = svc->Connect(profile_provider.NewRequest());
  if (status != ZX_OK) {
    return nullptr;
  }

  return std::make_unique<ProfileManager>(std::move(profile_provider));
}

ProfileManager::ProfileManager(fuchsia::scheduler::ProfileProviderSyncPtr profile_provider)
    : profile_provider_(std::move(profile_provider)) {}

zx_status_t ProfileManager::SetThreadAffinity(const zx::thread& thread, uint32_t mask) {
  return CreateAndApplyProfile<uint32_t>(
      &affinity_profiles_, mask,
      [this](uint32_t mask) -> zx::result<zx::profile> {
        zx::profile profile;
        zx_status_t server_status;
        zx_status_t status =
            profile_provider_->GetCpuAffinityProfile({mask}, &server_status, &profile);
        if (status != ZX_OK) {
          return zx::error(status);
        }
        if (server_status != ZX_OK) {
          return zx::error(server_status);
        }
        return zx::ok(std::move(profile));
      },
      thread);
}

zx_status_t ProfileManager::SetThreadAffinity(std::thread* thread, uint32_t mask) {
  return SetThreadAffinity(*HandleFromThread(thread), mask);
}

zx_status_t ProfileManager::SetThreadPriority(const zx::thread& thread, uint32_t priority) {
  return CreateAndApplyProfile<uint32_t>(
      &priority_profiles_, priority,
      [this](uint32_t priority) -> zx::result<zx::profile> {
        zx::profile profile;
        zx_status_t server_status;
        zx_status_t status = profile_provider_->GetProfile(
            priority, fxl::StringPrintf("hwstress-priority-%d", priority), &server_status,
            &profile);
        if (status != ZX_OK) {
          return zx::error(status);
        }
        if (server_status != ZX_OK) {
          return zx::error(server_status);
        }
        return zx::ok(std::move(profile));
      },
      thread);
}

zx_status_t ProfileManager::SetThreadPriority(std::thread* thread, uint32_t priority) {
  return SetThreadPriority(*HandleFromThread(thread), priority);
}

}  // namespace hwstress
