// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/global.h>
#include <lib/zx/process.h>

#include <memory>

#include "export.h"
#include "fx_logger.h"

namespace {

std::unique_ptr<fx_logger> g_logger_ptr;

}  // namespace

SYSLOG_EXPORT
fx_logger_t* fx_log_get_logger() { return g_logger_ptr.get(); }

SYSLOG_EXPORT
zx_status_t fx_log_init(void) {
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

  return fx_log_init_with_config(&config);
}

SYSLOG_EXPORT
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

SYSLOG_EXPORT
// This clears out global logger. This is used from tests
void fx_log_reset_global_for_testing() { g_logger_ptr.reset(nullptr); }

__END_CDECLS
