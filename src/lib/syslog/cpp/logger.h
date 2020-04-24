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

// Writes a message to the global logger.
// |severity| is one of DEBUG, INFO, WARNING, ERROR, FATAL
// |tag| is a tag to associated with the message, or NULL if none.
#define FX_LOGST(severity, tag) FXL_LOGT(severity, tag)

// Writes a message to the global logger.
// |severity| is one of DEBUG, INFO, WARNING, ERROR, FATAL
// |tag| is a tag to associated with the message, or NULL if none.
// |status| is a zx_status_t which will be appended in decimal and string forms
// after the message.
#define FX_PLOGST(severity, tag, status) FXL_PLOGT(severity, tag, status)

// Writes a message to the global logger.
// |severity| is one of DEBUG, INFO, WARNING, ERROR, FATAL
#define FX_LOGS(severity) FXL_LOG(severity)

// Writes a message to the global logger.
// |severity| is one of DEBUG, INFO, WARNING, ERROR, FATAL
// |status| is a zx_status_t which will be appended in decimal and string forms
// after the message.
#define FX_PLOGS(severity, status) FXL_PLOGT(severity, nullptr, status)

// Writes a message to the global logger, the first |n| times that any callsite
// of this macro is invoked. |n| should be a positive integer literal.
// |severity| is one of DEBUG, INFO, WARNING, ERROR, FATAL
#define FX_LOGS_FIRST_N(severity, n) FXL_LOG_FIRST_N(severity, n)

#define FX_LOGST_FIRST_N(severity, n, tag) FXL_LOGT_FIRST_N(severity, n, tag)

// Writes error message to the global logger if |condition| fails.
// |tag| is a tag to associated with the message, or NULL if none.
#define FX_CHECKT(condition, tag) FXL_CHECKT(condition, tag)

// Writes error message to the global logger if |condition| fails.
#define FX_CHECK(condition) FXL_CHECK(condition)

#define FX_DCHECK(condition) FXL_DCHECK(condition)

#define FX_DCHECKT(condition, tag) FXL_DCHECKT(condition, tag)

#define FX_DLOGS(severity) FXL_DLOG(severity)

#define FX_NOTREACHED() FXL_NOTREACHED()

#define FX_NOTIMPLEMENTED() FXL_NOTIMPLEMENTED()

#define FX_VLOGST(verbose_level, tag) FXL_VLOGT(verbose_level, tag)

#define FX_VLOGS(verbose_level) FXL_VLOG(verbose_level)

#endif  // SRC_LIB_SYSLOG_CPP_LOGGER_H_
