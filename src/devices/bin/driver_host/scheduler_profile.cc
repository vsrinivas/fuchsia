// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "scheduler_profile.h"

#include <fuchsia/scheduler/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/service/llcpp/service.h>
#include <lib/zx/channel.h>
#include <lib/zx/profile.h>
#include <lib/zx/thread.h>
#include <stdio.h>
#include <string.h>
#include <zircon/types.h>

namespace internal {

namespace {
fidl::ClientEnd<fuchsia_scheduler::ProfileProvider> scheduler_profile_provider;
}  // namespace

zx_status_t connect_scheduler_profile_provider() {
  zx::status client_end = service::Connect<fuchsia_scheduler::ProfileProvider>();
  if (client_end.is_ok()) {
    scheduler_profile_provider = std::move(client_end.value());
  }
  return client_end.status_value();
}

zx_status_t get_scheduler_profile(uint32_t priority, const char* name, zx_handle_t* profile) {
  fidl::WireResult result =
      fidl::WireCall(scheduler_profile_provider)
          .GetProfile(priority, fidl::StringView::FromExternal(name, strlen(name)));
  if (!result.ok()) {
    return result.status();
  }
  fidl::WireResponse response = std::move(result.value());
  if (response.status != ZX_OK) {
    return response.status;
  }
  *profile = response.profile.release();
  return ZX_OK;
}

zx_status_t get_scheduler_deadline_profile(uint64_t capacity, uint64_t deadline, uint64_t period,
                                           const char* name, zx_handle_t* profile) {
  fidl::WireResult result =
      fidl::WireCall(scheduler_profile_provider)
          .GetDeadlineProfile(capacity, deadline, period,
                              fidl::StringView::FromExternal(name, strlen(name)));
  if (!result.ok()) {
    return result.status();
  }
  fidl::WireResponse response = std::move(result.value());
  if (response.status != ZX_OK) {
    return response.status;
  }
  *profile = response.profile.release();
  return ZX_OK;
}

zx_status_t set_scheduler_profile_by_role(zx_handle_t thread, const char* role, size_t role_size) {
  zx::unowned_thread original_thread{thread};
  zx::thread duplicate_thread;
  zx_status_t status =
      original_thread->duplicate(ZX_RIGHT_TRANSFER | ZX_RIGHT_MANAGE_THREAD, &duplicate_thread);
  if (status != ZX_OK) {
    return status;
  }

  fidl::WireResult result = fidl::WireCall(scheduler_profile_provider)
                                .SetProfileByRole(std::move(duplicate_thread),
                                                  fidl::StringView::FromExternal(role, role_size));
  if (!result.ok()) {
    return result.status();
  }
  fidl::WireResponse response = std::move(result.value());
  if (response.status != ZX_OK) {
    return response.status;
  }
  return ZX_OK;
}
}  // namespace internal
