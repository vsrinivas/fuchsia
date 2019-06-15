// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/syslog/logger.h>
#include <lib/zx/channel.h>
#include <lib/zx/socket.h>

// TODO: Remove this hack once FIDL-182  is fixed.
typedef zx_handle_t fuchsia_logger_LogListener;
#include <fuchsia/logger/c/fidl.h>

#include "fx_logger.h"

namespace {

zx::socket connect_to_logger() {
    zx::socket invalid;
    zx::channel logger, logger_request;
    if (zx::channel::create(0, &logger, &logger_request) != ZX_OK) {
        return invalid;
    }
    if (fdio_service_connect("/svc/fuchsia.logger.LogSink", logger_request.release()) != ZX_OK) {
        return invalid;
    }
    zx::socket local, remote;
    if (zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote) != ZX_OK) {
        return invalid;
    }
    fuchsia_logger_LogSinkConnectRequest req;
    memset(&req, 0, sizeof(req));
    req.hdr.ordinal = fuchsia_logger_LogSinkConnectOrdinal;
    req.socket = FIDL_HANDLE_PRESENT;
    zx_handle_t handles[1] = {remote.release()};
    if (logger.write(0, &req, sizeof(req), handles, 1) != ZX_OK) {
        close(handles[0]);
        return invalid;
    }
    return local;
}

} // namespace

__EXPORT
zx_status_t fx_logger_logf(fx_logger_t* logger, fx_log_severity_t severity,
                           const char* tag, const char* format, ...) {
    if (logger == nullptr) {
        return ZX_ERR_BAD_STATE;
    }
    va_list args;
    va_start(args, format);
    zx_status_t s = logger->VLogWrite(severity, tag, format, args);
    va_end(args);
    return s;
}

__EXPORT
zx_status_t fx_logger_log(fx_logger_t* logger, fx_log_severity_t severity,
                          const char* tag, const char* msg) {
    if (logger == nullptr) {
        return ZX_ERR_BAD_STATE;
    }
    return logger->LogWrite(severity, tag, msg);
}

__EXPORT
zx_status_t fx_logger_logvf(fx_logger_t* logger, fx_log_severity_t severity,
                            const char* tag, const char* format, va_list args) {
    if (logger == nullptr) {
        return ZX_ERR_BAD_STATE;
    }
    return logger->VLogWrite(severity, tag, format, args);
}

__EXPORT
fx_log_severity_t fx_logger_get_min_severity(fx_logger_t* logger) {
    if (logger == nullptr) {
        return FX_LOG_FATAL;
    }
    return logger->GetSeverity();
}

__EXPORT
void fx_logger_set_min_severity(fx_logger_t* logger,
                                fx_log_severity_t severity) {
    return logger->SetSeverity(severity);
}

__EXPORT
void fx_logger_activate_fallback(fx_logger_t* logger,
                                 int fallback_fd) {
    logger->ActivateFallback(fallback_fd);
}

__EXPORT
zx_status_t fx_logger_create(const fx_logger_config_t* config,
                             fx_logger_t** out_logger) {
    if (config->num_tags > FX_LOG_MAX_TAGS) {
        return ZX_ERR_INVALID_ARGS;
    }
    fx_logger_config_t c = *config;
    if (config->console_fd == -1 &&
        config->log_service_channel == ZX_HANDLE_INVALID) {
        zx::socket sock = connect_to_logger();
        if (sock.is_valid()) {
            c.log_service_channel = sock.release();
        } else {
            int newfd = dup(STDERR_FILENO);
            if (newfd < 0) {
                return ZX_ERR_INTERNAL;
            }
            c.console_fd = newfd;
        }
    }
    *out_logger = new fx_logger(&c);
    return ZX_OK;
}

__EXPORT
void fx_logger_destroy(fx_logger_t* logger) {
    delete logger;
}
