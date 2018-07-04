// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/process-launcher/launcher.h>

#include "launcher.h"
#include <zircon/status.h>
#include <zircon/syscalls.h>

static zx_status_t connect(void* ctx, async_dispatcher_t* dispatcher, const char* service_name,
                           zx_handle_t request) {
    if (!strcmp(service_name, "fuchsia.process.Launcher")) {
        auto launcher = new launcher::LauncherImpl(zx::channel(request));

        zx_status_t status = launcher->Begin(dispatcher);
        if (status != ZX_OK) {
            delete launcher;
            return status;
        }

        launcher->set_error_handler([launcher](zx_status_t status) {
            // If we encounter an error, we tear down the launcher.
            delete launcher;
        });

        return ZX_OK;
    }

    zx_handle_close(request);
    return ZX_ERR_NOT_SUPPORTED;
}

static constexpr const char* launcher_services[] = {
    "fuchsia.process.Launcher",
    nullptr,
};

static constexpr zx_service_ops_t launcher_ops = {
    .init = nullptr,
    .connect = connect,
    .release = nullptr,
};

static constexpr zx_service_provider_t launcher_service_provider = {
    .version = SERVICE_PROVIDER_VERSION,
    .services = launcher_services,
    .ops = &launcher_ops,
};

const zx_service_provider_t* launcher_get_service_provider() {
    return &launcher_service_provider;
}
