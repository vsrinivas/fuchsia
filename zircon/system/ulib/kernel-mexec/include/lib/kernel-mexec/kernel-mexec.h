// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <functional>

#include <lib/svc/service.h>
#include <lib/zx/channel.h>

struct KernelMexecContext {
    zx_handle_t root_resource = 0;
    zx::unowned_channel devmgr_channel;
};

const zx_service_provider_t* kernel_mexec_get_service_provider(void);

// Exposed for testing.
namespace internal {

struct MexecSysCalls {
    std::function<zx_status_t(zx_handle_t, zx_handle_t, zx_handle_t)> mexec;
    std::function<zx_status_t(zx_handle_t, void*, size_t)> mexec_payload_get;
};

zx_status_t PerformMexec(void* ctx_raw, zx_handle_t raw_kernel, zx_handle_t raw_bootdata,
                         MexecSysCalls sys_calls);

}  // namespace internal
