// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Entry points used by clients.

#ifndef LIB_SYSLOG_GLOBAL_H_
#define LIB_SYSLOG_GLOBAL_H_

#include <lib/syslog/logger.h>

__BEGIN_CDECLS

// Gets the global logger for the process to which log messages emitted
// using the FX_LOG macros will be written. This function returns the same
// logger on all threads and is thread-safe. The returned pointer is never
// null and it does not get invalidated when the logger is reconfigured.
// The returned logger is guaranteed to have either a valid socket
// connection or be using the fallback mechanism (fd).
fx_logger_t* fx_log_get_logger(void);

// Returns true if writing messages with the given severity is enabled in the
// global logger.
static inline bool fx_log_is_enabled(fx_log_severity_t severity) {
  fx_logger_t* logger = fx_log_get_logger();
  return severity >= fx_logger_get_min_severity(logger);
}

// Get the severity corresponding to the given verbosity. Note that
// verbosity relative to the default severity and can be thought of
// as incrementally "more vebose than" the baseline.
static inline fx_log_severity_t fx_log_severity_from_verbosity(int verbosity) {
  if (verbosity < 0) {
    verbosity = 0;
  }
  // verbosity scale sits in the interstitial space between INFO and DEBUG
  int severity = FX_LOG_INFO - (verbosity * FX_LOG_VERBOSITY_STEP_SIZE);
  if (severity < FX_LOG_DEBUG + 1) {
    return FX_LOG_DEBUG + 1;
  }
  return severity;
}

// Returns true if writing messages with the given verbosity is enabled
// in the global logger.
static inline bool fx_vlog_is_enabled(int verbosity) {
  fx_logger_t* logger = fx_log_get_logger();
  return logger && (verbosity >= 0) &&
         fx_log_severity_from_verbosity(verbosity) >= fx_logger_get_min_severity(logger);
}

// Reconfigures the global logger for this process with the specified
// configuration.
// If |console_fd| and |log_service_channel| are invalid in |config|,
// this function doesn't change the currently used file descriptor or channel.
// |config| can be safely deallocated after this function returns.
// This function is NOT thread-safe and must be called early in the program
// before other threads are spawned.
// Returns:
// - ZX_ERR_INVALID_ARGS if config is invalid (i.e. is null or has more than
//   FX_LOG_MAX_TAGS tags),
// - ZX_OK if the reconfiguration succeeds.
zx_status_t fx_log_reconfigure(const fx_logger_config_t* config);

// Returns true if writing messages with the given severity is enabled in the
// global logger. |severity| is one of TRACE, DEBUG, INFO, WARNING, ERROR,
// or FATAL.
#define FX_LOG_IS_ENABLED(severity) (fx_log_is_enabled(FX_LOG_##severity))

// Returns true if writing messages with the given verbosity is enabled in the
// global logger. |verbosity| is an integer value > 0 up to a maximum of 15.
// (greater values will be truncated).
#define FX_VLOG_IS_ENABLED(verbosity) (fx_vlog_is_enabled(verbosity))

#define _FX_LOG_SET_SEVERITY(severity)                \
  do {                                                \
    fx_logger_t* logger = fx_log_get_logger();        \
    if (logger) {                                     \
      fx_logger_set_min_severity(logger, (severity)); \
    }                                                 \
  } while (0)

// Sets severity for global logger.
// |severity| is one of TRACE, DEBUG, INFO, WARNING, ERROR, or FATAL.
// Setting |verbosity| and |severity| both directly affect the log level
// so one or the other should be specified, but not both.
#define FX_LOG_SET_SEVERITY(severity) _FX_LOG_SET_SEVERITY(FX_LOG_##severity)

// Sets verbosity for global logger.
// |verbosity| is an integer 0-15.
// Setting |verbosity| and |severity| both directly affect the log level
// so one or the other should be specified, but not both.
#define FX_LOG_SET_VERBOSITY(verbosity) \
  _FX_LOG_SET_SEVERITY(fx_log_severity_from_verbosity(verbosity))

#define _FX_LOG(severity, tag, message)                     \
  do {                                                      \
    fx_logger_t* logger = fx_log_get_logger();              \
    if (fx_logger_get_min_severity(logger) <= (severity)) { \
      fx_logger_log(logger, (severity), (tag), (message));  \
    }                                                       \
  } while (0)

#define _FX_LOGF(severity, tag, message...)                 \
  do {                                                      \
    fx_logger_t* logger = fx_log_get_logger();              \
    if (fx_logger_get_min_severity(logger) <= (severity)) { \
      fx_logger_logf(logger, (severity), (tag), message);   \
    }                                                       \
  } while (0)

#define _FX_LOGVF(severity, tag, message, args)                      \
  do {                                                               \
    fx_logger_t* logger = fx_log_get_logger();                       \
    if (fx_logger_get_min_severity(logger) <= (severity)) {          \
      fx_logger_logvf(logger, (severity), (tag), (message), (args)); \
    }                                                                \
  } while (0)

// Writes a message to the global logger.
// |severity| is one of TRACE, DEBUG, INFO, WARNING, ERROR, FATAL
// |tag| is a tag to associated with the message, or NULL if none.
// |message| is the message to write, or NULL if none.
#define FX_LOG(severity, tag, message) _FX_LOG((FX_LOG_##severity), tag, message)

// Writes formatted message to the global logger.
// |severity| is one of TRACE, DEBUG, INFO, WARNING, ERROR, FATAL
// |tag| is a tag to associated with the message, or NULL if none.
// |message| is the message to write, or NULL if none.
#define FX_LOGF(severity, tag, message...) _FX_LOGF((FX_LOG_##severity), tag, message)

// Writes formatted message to the global logger using vaargs
// |severity| is one of TRACE, DEBUG, INFO, WARNING, ERROR, FATAL
// |tag| is a tag to associated with the message, or NULL if none.
// |message| is the message to write, or NULL if none.
// |args| are the arguments to |message|.
#define FX_LOGVF(severity, tag, message, args) _FX_LOGVF((FX_LOG_##severity), tag, message, args)

// Writes verbose message to the global logger.
// |verbosity| is an integer value > 0 up to a maximum of 15.
// |tag| is a tag to associated with the message, or NULL if none.
// |message| is the message to write, or NULL if none.
#define FX_VLOG(verbosity, tag, message) \
  _FX_LOG(fx_log_severity_from_verbosity(verbosity), tag, message)

// Writes formatted verbose message to the global logger.
// |verbosity| is an integer value > 0 up to a maximum of 15.
// |tag| is a tag to associated with the message, or NULL if none.
// |message| is the message to write, or NULL if none.
#define FX_VLOGF(verbosity, tag, message...) \
  _FX_LOGF(fx_log_severity_from_verbosity(verbosity), tag, message)

// Writes formatted verbose message to the global logger using vaargs
// |verbosity| is an integer value > 0 up to a maximum of 15.
// |tag| is a tag to associated with the message, or NULL if none.
// |message| is the message to write, or NULL if none.
// |args| are the arguments to |message|.
#define FX_VLOGVF(verbosity, tag, message, args) \
  _FX_LOGVF(fx_log_severity_from_verbosity(verbosity), tag, message, args)

__END_CDECLS

#endif  // LIB_SYSLOG_GLOBAL_H_
