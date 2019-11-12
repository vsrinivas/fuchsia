// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/logger.h>
#include <lib/zx/socket.h>

#include "export.h"
#include "fx_logger.h"

#ifndef STATIC_LIBRARY
#include "fdio_connect.h"
#endif

SYSLOG_EXPORT
zx_status_t fx_logger_logf(fx_logger_t* logger, fx_log_severity_t severity, const char* tag,
                           const char* format, ...) {
  if (logger == nullptr) {
    return ZX_ERR_BAD_STATE;
  }
  va_list args;
  va_start(args, format);
  zx_status_t s = logger->VLogWrite(severity, tag, format, args);
  va_end(args);
  return s;
}

SYSLOG_EXPORT
zx_status_t fx_logger_log(fx_logger_t* logger, fx_log_severity_t severity, const char* tag,
                          const char* msg) {
  if (logger == nullptr) {
    return ZX_ERR_BAD_STATE;
  }
  return logger->LogWrite(severity, tag, msg);
}

SYSLOG_EXPORT
zx_status_t fx_logger_logvf(fx_logger_t* logger, fx_log_severity_t severity, const char* tag,
                            const char* format, va_list args) {
  if (logger == nullptr) {
    return ZX_ERR_BAD_STATE;
  }
  return logger->VLogWrite(severity, tag, format, args);
}

SYSLOG_EXPORT
fx_log_severity_t fx_logger_get_min_severity(fx_logger_t* logger) {
  if (logger == nullptr) {
    return FX_LOG_FATAL;
  }
  return logger->GetSeverity();
}

SYSLOG_EXPORT
void fx_logger_set_min_severity(fx_logger_t* logger, fx_log_severity_t severity) {
  return logger->SetSeverity(severity);
}

SYSLOG_EXPORT
void fx_logger_activate_fallback(fx_logger_t* logger, int fallback_fd) {
  logger->ActivateFallback(fallback_fd);
}

SYSLOG_EXPORT
zx_status_t fx_logger_create(const fx_logger_config_t* config, fx_logger_t** out_logger) {
  if (config->num_tags > FX_LOG_MAX_TAGS) {
    return ZX_ERR_INVALID_ARGS;
  }
  fx_logger_config_t c = *config;
  if (config->console_fd == -1 && config->log_service_channel == ZX_HANDLE_INVALID) {
#ifdef SYSLOG_STATIC
    return ZX_ERR_INVALID_ARGS;
#else
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
#endif
  }
  *out_logger = new fx_logger(&c);
  return ZX_OK;
}

SYSLOG_EXPORT
void fx_logger_destroy(fx_logger_t* logger) { delete logger; }
