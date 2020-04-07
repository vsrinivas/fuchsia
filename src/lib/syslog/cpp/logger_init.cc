// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/global.h>
#include <lib/syslog/logger.h>

#include <initializer_list>
#include <string>

#include "logger.h"

namespace syslog {

zx_status_t SetSettings(const syslog::LogSettings& settings,
                        const std::initializer_list<std::string>& tags) {
  if (tags.size() > FX_LOG_MAX_TAGS) {
    return ZX_ERR_INVALID_ARGS;
  }
  const char* ctags[FX_LOG_MAX_TAGS];
  int i = 0;
  for (auto& tag : tags) {
    ctags[i++] = tag.c_str();
  }
  fx_logger_config_t config = {.min_severity = settings.severity,
                               .console_fd = settings.fd,
                               .log_service_channel = ZX_HANDLE_INVALID,
                               .tags = ctags,
                               .num_tags = tags.size()};
  return fx_log_reconfigure(&config);
}

zx_status_t SetTags(const std::initializer_list<std::string>& tags) {
  fx_logger_t* logger = fx_log_get_logger();
  LogSettings settings = {.severity = fx_logger_get_min_severity(logger), .fd = -1};
  return SetSettings(settings, tags);
}

zx_status_t InitLogger(const syslog::LogSettings& settings,
                       const std::initializer_list<std::string>& tags) {
  return SetSettings(settings, tags);
}

zx_status_t InitLogger(const std::initializer_list<std::string>& tags) { return SetTags(tags); }

}  // namespace syslog
