// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/global.h>
#include <lib/zx/process.h>

#include <memory>

#include "export.h"
#include "fx_logger.h"

namespace {

fx_logger_t* MakeDefaultLogger() {
  char process_name[ZX_MAX_NAME_LEN] = "";
  const char* tag = process_name;

  zx_status_t status =
      zx::process::self()->get_property(ZX_PROP_NAME, process_name, sizeof(process_name));
  if (status != ZX_OK)
    process_name[0] = '\0';

  fx_logger_config_t config = {.min_severity = FX_LOG_INFO,
                               .console_fd = -1,
                               .log_service_channel = ZX_HANDLE_INVALID,
                               .tags = &tag,
                               .num_tags = 1};
  fx_logger_t* logger = NULL;
  fx_logger_create(&config, &logger);
  return logger;
}

}  // namespace

SYSLOG_EXPORT
fx_logger_t* fx_log_get_logger() {
  static std::unique_ptr<fx_logger_t> logger(MakeDefaultLogger());
  return logger.get();
}

SYSLOG_EXPORT
zx_status_t fx_log_init(void) {
  if (!fx_log_get_logger())
    return ZX_ERR_INTERNAL;
  return ZX_OK;
}

SYSLOG_EXPORT
zx_status_t fx_log_init_with_config(const fx_logger_config_t* config) {
  fx_logger_t* logger = fx_log_get_logger();
  if (!logger)
    return ZX_ERR_INTERNAL;
  return logger->Reconfigure(config);
}

// This is here to force a definition to be included here for C99.
extern inline bool fx_log_is_enabled(fx_log_severity_t severity);

__BEGIN_CDECLS

SYSLOG_EXPORT
// This clears out global logger. This is used from tests
void fx_log_reset_global_for_testing() {
  // TODO(samans): Remove this function since it is no longer necessary to destroy the
  // global logger in order to reconfigure logging. https://fxbug.dev/49001
}

__END_CDECLS
