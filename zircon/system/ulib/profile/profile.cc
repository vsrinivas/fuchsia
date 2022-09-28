// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon/syscalls/profile.h"

#include <fuchsia/scheduler/c/fidl.h>
#include <inttypes.h>
#include <lib/fidl-async/bind.h>
#include <lib/fitx/result.h>
#include <lib/profile/profile.h>
#include <lib/syslog/global.h>
#include <lib/zx/profile.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <algorithm>
#include <iterator>
#include <string>

#include "zircon/system/ulib/profile/config.h"

namespace {

constexpr char kConfigPath[] = "/config/profiles";

using zircon_profile::MaybeMediaRole;
using zircon_profile::ParseRoleSelector;
using zircon_profile::ProfileMap;

struct Context {
  const zx_handle_t root_job;
  ProfileMap profiles;

  static Context* Get(void* ctx) { return static_cast<Context*>(ctx); }
};

zx_status_t GetProfileSimple(void* ctx, uint32_t priority, const char* name_data, size_t name_size,
                             fidl_txn_t* txn) {
  Context* const context = Context::Get(ctx);

  const std::string name{name_data, name_size};
  FX_LOGF(INFO, "ProfileProvider", "\"%s\" requested priority %u", name.c_str(), priority);

  zx_profile_info_t info = {};
  info.flags = ZX_PROFILE_INFO_FLAG_PRIORITY;
  info.priority =
      std::min<uint32_t>(std::max<uint32_t>(priority, ZX_PRIORITY_LOWEST), ZX_PRIORITY_HIGHEST);

  zx::profile profile;
  zx_status_t status =
      zx_profile_create(context->root_job, 0u, &info, profile.reset_and_get_address());
  return fuchsia_scheduler_ProfileProviderGetProfile_reply(
      txn, status, status == ZX_OK ? profile.release() : ZX_HANDLE_INVALID);
}

zx_status_t GetDeadlineProfileSimple(void* ctx, uint64_t capacity, uint64_t relative_deadline,
                                     uint64_t period, const char* name_data, size_t name_size,
                                     fidl_txn_t* txn) {
  Context* const context = Context::Get(ctx);

  const std::string name{name_data, name_size};
  const double utilization = static_cast<double>(capacity) / static_cast<double>(relative_deadline);
  FX_LOGF(INFO, "ProfileProvider",
          "\"%s\" requested capacity %" PRIu64 " deadline %" PRIu64 " period %" PRIu64
          " utilization %f",
          name.c_str(), capacity, relative_deadline, period, utilization);

  zx_profile_info_t info = {};
  info.flags = ZX_PROFILE_INFO_FLAG_DEADLINE;
  info.deadline_params.capacity = capacity;
  info.deadline_params.relative_deadline = relative_deadline;
  info.deadline_params.period = period;

  zx::profile profile;
  zx_status_t status =
      zx_profile_create(context->root_job, 0u, &info, profile.reset_and_get_address());
  return fuchsia_scheduler_ProfileProviderGetDeadlineProfile_reply(
      txn, status, status == ZX_OK ? profile.release() : ZX_HANDLE_INVALID);
}

zx_status_t GetCpuAffinityProfileSimple(void* ctx, const fuchsia_scheduler_CpuSet* cpu_mask,
                                        fidl_txn_t* txn) {
  Context* const context = Context::Get(ctx);

  zx_profile_info_t info = {};
  info.flags = ZX_PROFILE_INFO_FLAG_CPU_MASK;

  static_assert(sizeof(info.cpu_affinity_mask.mask) == sizeof(cpu_mask->mask));
  static_assert(std::size(info.cpu_affinity_mask.mask) == std::size(decltype(cpu_mask->mask){}));
  memcpy(info.cpu_affinity_mask.mask, cpu_mask->mask, sizeof(cpu_mask->mask));

  zx::profile profile;
  zx_status_t status =
      zx_profile_create(context->root_job, 0u, &info, profile.reset_and_get_address());
  return fuchsia_scheduler_ProfileProviderGetCpuAffinityProfile_reply(
      txn, status, status == ZX_OK ? profile.release() : ZX_HANDLE_INVALID);
}

zx_status_t SetProfileByRoleSimple(void* ctx, zx_handle_t thread, const char* role_data,
                                   size_t role_size, fidl_txn_t* txn) {
  Context* const context = Context::Get(ctx);

  // Log the requested role and PID:TID of the thread being assigned.
  zx_info_handle_basic_t handle_info{};
  zx_status_t status = zx_object_get_info(thread, ZX_INFO_HANDLE_BASIC, &handle_info,
                                          sizeof(handle_info), nullptr, nullptr);
  if (status != ZX_OK) {
    FX_LOGF(WARNING, "ProfileProvider", "Failed to get info for thread handle: %s",
            zx_status_get_string(status));
    handle_info.koid = ZX_KOID_INVALID;
    handle_info.related_koid = ZX_KOID_INVALID;
  }

  const std::string role_selector{role_data, role_size};
  FX_LOGF(INFO, "ProfileProvider", "Role \"%s\" requested by %" PRId64 ":%" PRId64,
          role_selector.c_str(), handle_info.related_koid, handle_info.koid);

  const fitx::result role_result = ParseRoleSelector(role_selector);
  if (role_result.is_error()) {
    return fuchsia_scheduler_ProfileProviderSetProfileByRole_reply(txn, ZX_ERR_INVALID_ARGS);
  }

  // Select the profile parameters based on the role selector. The builtin roles cannot be
  // overridden.
  zx_profile_info_t info = {};
  if (role_result->name == "fuchsia.default") {
    info.flags = ZX_PROFILE_INFO_FLAG_PRIORITY;
    info.priority = ZX_PRIORITY_DEFAULT;
  } else if (role_result->name == "fuchsia.test-role" && role_result->has("not-found")) {
    return fuchsia_scheduler_ProfileProviderSetProfileByRole_reply(txn, ZX_ERR_NOT_FOUND);
  } else if (role_result->name == "fuchsia.test-role" && role_result->has("ok")) {
    return fuchsia_scheduler_ProfileProviderSetProfileByRole_reply(txn, ZX_OK);
  } else if (auto search = context->profiles.find(role_result->name);
             search != context->profiles.cend()) {
    info = search->second.info;
  } else if (const auto media_role = MaybeMediaRole(*role_result); media_role.is_ok()) {
    // TODO(fxbug.dev/40858): If a media profile is not found in the system config, use the
    // forwarded parameters. This can be removed once clients are migrated to use defined roles.
    FX_LOGF(INFO, "ProfileProvider", "No media profile override, using selector parameters: %s",
            role_selector.c_str());
    info.flags = ZX_PROFILE_INFO_FLAG_DEADLINE;
    info.deadline_params.capacity = media_role->capacity;
    info.deadline_params.relative_deadline = media_role->deadline;
    info.deadline_params.period = media_role->deadline;
  } else {
    FX_LOGF(WARNING, "ProfileProvider", "Requested role \"%s\" not found!",
            role_result->name.c_str());
    return fuchsia_scheduler_ProfileProviderSetProfileByRole_reply(txn, ZX_ERR_NOT_FOUND);
  }

  zx::profile profile;
  status = zx_profile_create(context->root_job, 0u, &info, profile.reset_and_get_address());
  if (status != ZX_OK) {
    // Failing to create a profile is likely due to a programming error in
    // this handler. The most likely cause is invalid profile parameters.
    return fuchsia_scheduler_ProfileProviderSetProfileByRole_reply(txn, ZX_ERR_INTERNAL);
  }

  status = zx_object_set_profile(thread, profile.get(), 0);
  return fuchsia_scheduler_ProfileProviderSetProfileByRole_reply(txn, status);
}

fuchsia_scheduler_ProfileProvider_ops ops = {
    .GetProfile = GetProfileSimple,
    .GetDeadlineProfile = GetDeadlineProfileSimple,
    .GetCpuAffinityProfile = GetCpuAffinityProfileSimple,
    .SetProfileByRole = SetProfileByRoleSimple,
};

constexpr const char* profile_svc_names[] = {
    fuchsia_scheduler_ProfileProvider_Name,
    nullptr,
};

zx_status_t init(void** out_ctx) {
  const auto root_job = static_cast<zx_handle_t>(reinterpret_cast<uintptr_t>(*out_ctx));

  auto result = zircon_profile::LoadConfigs(kConfigPath);
  if (result.is_error()) {
    FX_LOGF(ERROR, "ProfileProvider", "Failed to load configs: %s", result.error_value().c_str());
    return ZX_ERR_INTERNAL;
  }

  // Apply the dispatch role if defined.
  const std::string dispatch_role = "fuchsia.system.profile-provider.dispatch";
  const auto search = result->find(dispatch_role);
  if (search != result->end()) {
    FX_LOGF(INFO, "ProfileProvider", "Role \"%s\" is defined. Applying to dispatcher.",
            dispatch_role.c_str());

    zx::profile profile;
    zx_status_t status =
        zx_profile_create(root_job, 0u, &search->second.info, profile.reset_and_get_address());
    if (status != ZX_OK) {
      FX_LOGF(ERROR, "ProfileProvider", "Failed to create profile for role \"%s\": %s",
              dispatch_role.c_str(), zx_status_get_string(status));
    } else {
      status = zx_object_set_profile(zx_thread_self(), profile.get(), 0);
      if (status != ZX_OK) {
        FX_LOGF(ERROR, "ProfileProvider", "Failed to set profile: %s",
                zx_status_get_string(status));
      }
    }
  }

  *out_ctx = new Context{root_job, std::move(result.value())};
  return ZX_OK;
}

zx_status_t connect(void* ctx, async_dispatcher_t* dispatcher, const char* service_name,
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

constexpr zx_service_ops_t service_ops = {
    .init = init,
    .connect = connect,
    .release = nullptr,
};

constexpr zx_service_provider_t profile_service_provider = {
    .version = SERVICE_PROVIDER_VERSION,
    .services = profile_svc_names,
    .ops = &service_ops,
};

}  // namespace

const zx_service_provider_t* profile_get_service_provider() { return &profile_service_provider; }
