// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scenic/util/scheduler_profile.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <lib/zx/result.h>
#include <lib/zx/thread.h>

#include <optional>

#include "fuchsia/scheduler/cpp/fidl.h"
#include "lib/fdio/directory.h"

namespace {

zx::result<fuchsia::scheduler::ProfileProvider_SyncProxy*> GetProfileProvider() {
  static std::optional<fuchsia::scheduler::ProfileProvider_SyncProxy> provider;
  if (provider) {
    return zx::ok(&provider.value());
  }

  // Connect to the scheduler profile service to request a new profile.
  zx::channel channel0, channel1;
  zx_status_t status;

  status = zx::channel::create(0u, &channel0, &channel1);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create channel pair: " << status;
    return zx::error_result(status);
  }

  status = fdio_service_connect(
      (std::string("/svc/") + fuchsia::scheduler::ProfileProvider::Name_).c_str(),
      channel0.release());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to connect to profile provider: " << status;
    return zx::error_result(status);
  }

  provider.emplace(std::move(channel1));
  return zx::ok(&provider.value());
}

}  // anonymous namespace

namespace util {

zx_status_t SetSchedulerRole(const zx::unowned_thread& thread, const std::string& role) {
  zx::result provider = GetProfileProvider();
  if (provider.is_error()) {
    return provider.error_value();
  }

  zx::thread duplicate_handle;
  zx_status_t status = thread->duplicate(ZX_RIGHT_SAME_RIGHTS, &duplicate_handle);
  if (status != ZX_OK) {
    return status;
  }

  zx_status_t fidl_status = ZX_OK;
  status = provider->SetProfileByRole(std::move(duplicate_handle), role, &fidl_status);
  if (status != ZX_OK) {
    FX_LOGS(WARNING) << "Failed to request profile: status=" << status;
    return status;
  }
  if (fidl_status != ZX_OK) {
    FX_LOGS(WARNING) << "Failed to request profile: fidl_status=" << fidl_status;
    return fidl_status;
  }

  return ZX_OK;
}

}  // namespace util
