// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon/syscalls/profile.h"

#include <fuchsia/scheduler/c/fidl.h>
#include <lib/fidl-async/bind.h>
#include <lib/profile/profile.h>
#include <lib/syslog/global.h>
#include <lib/zx/profile.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include <algorithm>

namespace {

zx_status_t GetProfileSimple(void* ctx, uint32_t priority, const char* /*name_data*/,
                             size_t /*name_size*/, fidl_txn_t* txn) {
  auto root_job = static_cast<zx_handle_t>(reinterpret_cast<uintptr_t>(ctx));

  zx_profile_info_t info = {};
  info.flags = ZX_PROFILE_INFO_FLAG_PRIORITY;
  info.priority =
      std::min<uint32_t>(std::max<uint32_t>(priority, ZX_PRIORITY_LOWEST), ZX_PRIORITY_HIGHEST);

  zx::profile profile;
  zx_status_t status = zx_profile_create(root_job, 0u, &info, profile.reset_and_get_address());
  return fuchsia_scheduler_ProfileProviderGetProfile_reply(
      txn, status, status == ZX_OK ? profile.release() : ZX_HANDLE_INVALID);
}

zx_status_t GetDeadlineProfileSimple(void* ctx, uint64_t capacity, uint64_t relative_deadline,
                                     uint64_t period, const char* /*name_data*/,
                                     size_t /*name_size*/, fidl_txn_t* txn) {
  auto root_job = static_cast<zx_handle_t>(reinterpret_cast<uintptr_t>(ctx));

  zx_profile_info_t info = {};
  info.flags = ZX_PROFILE_INFO_FLAG_DEADLINE;
  info.deadline_params.capacity = capacity;
  info.deadline_params.relative_deadline = relative_deadline;
  info.deadline_params.period = period;

  zx::profile profile;
  zx_status_t status = zx_profile_create(root_job, 0u, &info, profile.reset_and_get_address());
  return fuchsia_scheduler_ProfileProviderGetDeadlineProfile_reply(
      txn, status, status == ZX_OK ? profile.release() : ZX_HANDLE_INVALID);
}

zx_status_t GetCpuAffinityProfileSimple(void* ctx, const fuchsia_scheduler_CpuSet* cpu_mask,
                                        fidl_txn_t* txn) {
  auto root_job = static_cast<zx_handle_t>(reinterpret_cast<uintptr_t>(ctx));

  zx_profile_info_t info = {};
  info.flags = ZX_PROFILE_INFO_FLAG_CPU_MASK;

  static_assert(sizeof(info.cpu_affinity_mask.mask) == sizeof(cpu_mask->mask));
  static_assert(countof(info.cpu_affinity_mask.mask) == countof(cpu_mask->mask));
  memcpy(info.cpu_affinity_mask.mask, cpu_mask->mask, sizeof(cpu_mask->mask));

  zx::profile profile;
  zx_status_t status = zx_profile_create(root_job, 0u, &info, profile.reset_and_get_address());
  return fuchsia_scheduler_ProfileProviderGetCpuAffinityProfile_reply(
      txn, status, status == ZX_OK ? profile.release() : ZX_HANDLE_INVALID);
}

fuchsia_scheduler_ProfileProvider_ops ops = {
    .GetProfile = GetProfileSimple,
    .GetDeadlineProfile = GetDeadlineProfileSimple,
    .GetCpuAffinityProfile = GetCpuAffinityProfileSimple,
};

constexpr const char* profile_svc_names[] = {
    fuchsia_scheduler_ProfileProvider_Name,
    nullptr,
};

}  // namespace

static zx_status_t init(void** /*out_ctx*/) {
  // *out_ctx is already the root job handle, don't nuke it.
  return ZX_OK;
}

static zx_status_t connect(void* ctx, async_dispatcher_t* dispatcher, const char* service_name,
                           zx_handle_t request) {
  if (strcmp(service_name, fuchsia_scheduler_ProfileProvider_Name) == 0) {
    auto callback_adapter = [](void* ctx, fidl_txn* txn, fidl_incoming_msg* msg,
                               const void* ops) -> int {
      const auto* provider_ops = static_cast<const fuchsia_scheduler_ProfileProvider_ops_t*>(ops);
      return fuchsia_scheduler_ProfileProvider_dispatch(ctx, txn, msg, provider_ops);
    };
    return fidl_bind(dispatcher, request, callback_adapter, ctx, &ops);
  }

  zx_handle_close(request);
  return ZX_ERR_NOT_SUPPORTED;
}

static constexpr zx_service_ops_t service_ops = {
    .init = init,
    .connect = connect,
    .release = nullptr,
};

static constexpr zx_service_provider_t profile_service_provider = {
    .version = SERVICE_PROVIDER_VERSION,
    .services = profile_svc_names,
    .ops = &service_ops,
};

const zx_service_provider_t* profile_get_service_provider() { return &profile_service_provider; }
