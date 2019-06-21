// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/profile/profile.h>

#include <algorithm>

#include <fuchsia/scheduler/c/fidl.h>
#include <lib/fidl-async/bind.h>
#include <lib/syslog/global.h>
#include <lib/zx/profile.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

namespace {

zx_status_t GetProfileSimple(void* ctx, uint32_t priority, const char* name_data, size_t name_size,
                             fidl_txn_t* txn) {
    zx_handle_t root_job = static_cast<zx_handle_t>(reinterpret_cast<uintptr_t>(ctx));

    // TODO(scottmg): More things here.
    zx_profile_info_t info = {
        .type = ZX_PROFILE_INFO_SCHEDULER,
        {.scheduler = {
             .priority = std::min(std::max(static_cast<int32_t>(priority), ZX_PRIORITY_LOWEST),
                                  ZX_PRIORITY_HIGHEST),
             .boost = 0,
             .deboost = 0,
             .quantum = 0,
         }}};

    zx::profile profile;
    zx_status_t status = zx_profile_create(root_job, 0u, &info, profile.reset_and_get_address());
    return fuchsia_scheduler_ProfileProviderGetProfile_reply(
        txn, status, status == ZX_OK ? profile.release() : ZX_HANDLE_INVALID);
}

fuchsia_scheduler_ProfileProvider_ops ops = {.GetProfile = GetProfileSimple};

constexpr const char* profile_svc_names[] = {
    fuchsia_scheduler_ProfileProvider_Name,
    nullptr,
};

} // namespace

static zx_status_t init(void** out_ctx) {
    // *out_ctx is already the root job handle, don't nuke it.
    return ZX_OK;
}

static zx_status_t connect(void* ctx, async_dispatcher_t* dispatcher, const char* service_name,
                           zx_handle_t request) {
    if (strcmp(service_name, fuchsia_scheduler_ProfileProvider_Name) == 0) {
        return fidl_bind(dispatcher, request,
                         (fidl_dispatch_t*)fuchsia_scheduler_ProfileProvider_dispatch, ctx, &ops);
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

const zx_service_provider_t* profile_get_service_provider() {
    return &profile_service_provider;
}
