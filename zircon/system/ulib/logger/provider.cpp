// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/logger/provider.h>

#include <lib/logger/logger.h>
#include <fuchsia/logger/c/fidl.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

static zx_status_t connect(void* ctx, async_dispatcher_t* dispatcher, const char* service_name,
                           zx_handle_t request) {
    if (!strcmp(service_name, fuchsia_logger_LogSink_Name)) {
        auto logger = new logger::LoggerImpl(zx::channel(request), STDOUT_FILENO);

        zx_status_t status = logger->Begin(dispatcher);
        if (status != ZX_OK) {
            delete logger;
            return status;
        }

        logger->set_error_handler([logger](zx_status_t status) {
            // If we encounter an error, we tear down the logger.
            delete logger;
        });

        return ZX_OK;
    }

    zx_handle_close(request);
    return ZX_ERR_NOT_SUPPORTED;
}

static constexpr const char* logger_services[] = {
    fuchsia_logger_LogSink_Name,
    nullptr,
};

static constexpr zx_service_ops_t logger_ops = {
    .init = nullptr,
    .connect = connect,
    .release = nullptr,
};

static constexpr zx_service_provider_t logger_service_provider = {
    .version = SERVICE_PROVIDER_VERSION,
    .services = logger_services,
    .ops = &logger_ops,
};

const zx_service_provider_t* logger_get_service_provider() {
    return &logger_service_provider;
}
