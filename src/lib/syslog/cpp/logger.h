// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_SYSLOG_CPP_LOGGER_H_
#define SRC_LIB_SYSLOG_CPP_LOGGER_H_

#include "src/lib/fxl/log_settings.h"
#include "src/lib/fxl/logging.h"

namespace syslog {

struct LogSettings {
  fxl::LogSeverity severity;
  // Ignored:
  int fd;
};

// Sets the settings and tags for the global logger.
inline void SetSettings(const syslog::LogSettings& settings,
                        const std::initializer_list<std::string>& tags) {
  fxl::SetLogSettings({.min_log_level = settings.severity});
}

// Sets the tags for the global logger.
inline void SetTags(const std::initializer_list<std::string>& tags) { fxl::SetLogTags(tags); }

}  // namespace syslog

#endif  // SRC_LIB_SYSLOG_CPP_LOGGER_H_
