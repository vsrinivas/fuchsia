// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/logger.h>
#include <lib/zx/socket.h>

#include "export.h"
#include "fx_logger.h"

#ifndef SYSLOG_STATIC
#include <fidl/fuchsia.logger/cpp/wire.h>
#include <lib/component/cpp/incoming/service_client.h>
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
zx_status_t fx_logger_logf_with_source(fx_logger_t* logger, fx_log_severity_t severity,
                                       const char* tag, const char* file, int line,
                                       const char* format, ...) {
  if (!logger || !file || line <= 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  va_list args;
  va_start(args, format);
  zx_status_t s = logger->VLogWrite(severity, tag, format, args, file, line);
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
zx_status_t fx_logger_log_with_source(fx_logger_t* logger, fx_log_severity_t severity,
                                      const char* tag, const char* file, int line,
                                      const char* msg) {
  if (!logger || !file || line <= 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  return logger->LogWrite(severity, tag, msg, file, line);
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
zx_status_t fx_logger_logvf_with_source(fx_logger_t* logger, fx_log_severity_t severity,
                                        const char* tag, const char* file, int line,
                                        const char* format, va_list args) {
  if (!logger || !file || line <= 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  return logger->VLogWrite(severity, tag, format, args, file, line);
}

SYSLOG_EXPORT
fx_log_severity_t fx_logger_get_min_severity(fx_logger_t* logger) {
  if (logger == nullptr) {
    return FX_LOG_FATAL;
  }
  return logger->GetSeverity();
}

SYSLOG_EXPORT
zx_status_t fx_logger_set_min_severity(fx_logger_t* logger, fx_log_severity_t severity) {
  return logger->SetSeverity(severity);
}

SYSLOG_EXPORT
zx_status_t fx_logger_get_connection_status(fx_logger_t* logger) {
  return logger->GetLogConnectionStatus();
}

SYSLOG_EXPORT
void fx_logger_set_connection(fx_logger_t* logger, zx_handle_t handle) {
  return logger->SetLogConnection(handle);
}

SYSLOG_EXPORT
void fx_logger_activate_fallback(fx_logger_t* logger, int fallback_fd) {
  logger->ActivateFallback(fallback_fd);
}

SYSLOG_EXPORT
zx_status_t fx_logger_reconfigure(fx_logger_t* logger, const fx_logger_config_t* config) {
  if (!config) {
    return ZX_ERR_INVALID_ARGS;
  }
  return logger->Reconfigure(config, false);
}

SYSLOG_EXPORT
zx_status_t fx_logger_reconfigure_structured(fx_logger_t* logger,
                                             const fx_logger_config_t* config) {
  return logger->Reconfigure(config, true);
}

SYSLOG_EXPORT
zx_status_t fx_logger_create_internal(const fx_logger_config_t* config, fx_logger_t** out_logger) {
  // TODO(https://fxbug.dev/65995): Share the input checks and handle closing logic with
  // fx_logger::Reconfigure().
  if (!config || !out_logger) {
    return ZX_ERR_INVALID_ARGS;
  }

  if ((config->num_tags > FX_LOG_MAX_TAGS) ||
#ifndef SYSLOG_STATIC
      (config->log_sink_channel != ZX_HANDLE_INVALID &&
       config->log_sink_socket != ZX_HANDLE_INVALID)
#else
      // |log_sink_channel| is not supported by SYSLOG_STATIC.
      config->log_sink_channel != ZX_HANDLE_INVALID
#endif
  ) {
    if (config->log_sink_channel != ZX_HANDLE_INVALID) {
      zx_handle_close(config->log_sink_channel);
    }
    if (config->log_sink_socket != ZX_HANDLE_INVALID) {
      zx_handle_close(config->log_sink_socket);
    }
    return ZX_ERR_INVALID_ARGS;
  }
  bool is_structured = false;
  auto c = *config;
  // In the SYSLOG_STATIC mode, we cannot connect to the logging service. We
  // should continue to instantiate the logger (which defaults to using stderr)
  // and the client can provide the appropriate channel / fd later.
#ifndef SYSLOG_STATIC
  if (config->log_sink_channel == ZX_HANDLE_INVALID &&
      config->log_sink_socket == ZX_HANDLE_INVALID) {
    zx::result socket = []() -> zx::result<zx::socket> {
      zx::result logger = component::Connect<fuchsia_logger::LogSink>();
      if (logger.is_error()) {
        return logger.take_error();
      }
      zx::socket local, remote;
      if (zx_status_t status = zx::socket::create(ZX_SOCKET_DATAGRAM, &local, &remote);
          status != ZX_OK) {
        return zx::error(status);
      }
      const fidl::WireResult result =
          fidl::WireCall(logger.value())->ConnectStructured(std::move(remote));
      if (!result.ok()) {
        return zx::error(result.status());
      }
      return zx::ok(std::move(local));
    }();
    if (socket.is_ok()) {
      c.log_sink_socket = socket.value().release();
      is_structured = true;
    }
  }
#endif
  *out_logger = new fx_logger(&c, is_structured);
  return ZX_OK;
}

// Retrieves the tag at the specified index or nullptr if no
// such tag exists.
SYSLOG_EXPORT
void fx_logger_get_tags(fx_logger_t* logger, void (*callback)(void* context, const char* tag),
                        void* context) {
  logger->GetTags([=](const fbl::String& tag) { callback(context, tag.c_str()); });
}

SYSLOG_EXPORT
zx_status_t fx_logger_create(const fx_logger_config_t* config, fx_logger_t** out_logger) {
  return fx_logger_create_internal(config, out_logger);
}

SYSLOG_EXPORT
void fx_logger_destroy(fx_logger_t* logger) { delete logger; }
