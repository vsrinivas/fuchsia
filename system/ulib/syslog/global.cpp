// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/unique_ptr.h>

#include <syslog/global.h>

#include "fx_logger.h"

namespace {

fbl::unique_ptr<fx_logger> g_logger_ptr;

} // namespace

fx_logger_t* fx_log_get_logger() {
    return g_logger_ptr.get();
}

zx_status_t fx_log_init(void) {
    fx_logger_config_t config = {.min_severity = FX_LOG_INFO,
                                 .console_fd = -1,
                                 .log_service_channel = ZX_HANDLE_INVALID,
                                 .tags = NULL,
                                 .num_tags = 0};

    return fx_log_init_with_config(&config);
}

zx_status_t fx_log_init_with_config(const fx_logger_config_t* config) {
    if (config == nullptr) {
        return ZX_ERR_BAD_STATE;
    }
    if (g_logger_ptr.get()) {
        return ZX_ERR_BAD_STATE;
    }
    fx_logger_t* logger = NULL;
    auto status = fx_logger_create(config, &logger);
    if (status != ZX_OK) {
        return status;
    }
    g_logger_ptr.reset(logger);
    return ZX_OK;
}

// This is here to force a definition to be included here for C99.
extern inline bool fx_log_is_enabled(fx_log_severity_t severity);

__BEGIN_CDECLS

// This clears out global logger. This is used from tests
void fx_log_reset_global() {
    g_logger_ptr.reset(nullptr);
}

__END_CDECLS
