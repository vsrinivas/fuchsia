// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/unique_ptr.h>

#include <syslog/global.h>

#include "fx_logger.h"

namespace syslog {
namespace {
fbl::unique_ptr<fx_logger> g_logger_ptr;
}  // namespace
}  // namespace syslog

fx_logger_t* fx_log_get_logger() { return syslog::g_logger_ptr.get(); }

zx_status_t fx_log_init_with_config(const fx_logger_config_t* config) {
  if (config == nullptr) {
    return ZX_ERR_BAD_STATE;
  }
  if (syslog::g_logger_ptr.get()) {
    return ZX_ERR_BAD_STATE;
  }
  fx_logger_t* logger = NULL;
  auto status = fx_logger_create(config, &logger);
  if (status != ZX_OK) {
    return status;
  }
  syslog::g_logger_ptr.reset(logger);
  return ZX_OK;
}

__BEGIN_CDECLS

// This clears out global logger. This is used from tests
void fx_log_reset_global() { syslog::g_logger_ptr.reset(nullptr); }

__END_CDECLS
