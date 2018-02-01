// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Entry points used by clients.

#pragma once

#include "logger.h"

__BEGIN_CDECLS

// Gets the global logger for the process to which log messages emitted
// using the FX_LOG macros will be written.  Returns NULL if logging is
// not configured.
fx_logger_t* fx_log_get_logger(void);

// Returns true if writing messages with the given severity is enabled in the
// global logger.
inline bool fx_log_is_enabled(fx_log_severity_t severity) {
  fx_logger_t* logger = fx_log_get_logger();
  return logger && severity >= fx_logger_get_min_severity(logger);
}

// Initializes the logging infrastructure with the specified configuration.
// Returns |ZX_ERR_BAD_STATE| if logging has already been initialized.
// If |console_fd| and |log_service_channel| are invalid in |config|,
// this function chooses a default destination for the log.
// |config| can be safely deallocated after this function returns.
//
// global logger would be deallocated once program ends.
zx_status_t fx_log_init_with_config(const fx_logger_config_t* config);

// Initializes the logging infrastructure for this process using default
// parameters. Returns |ZX_ERR_BAD_STATE| if logging has already been
// initialized.
//
// global logger would be deallocated once program ends.
zx_status_t fx_log_init(void);

// Returns true if writing messages with the given severity is enabled in the
// global logger. |severity| is one of DEBUG, INFO, WARNING, ERROR, or FATAL.
#define FX_LOG_IS_ENABLED(severity) (fx_log_is_enabled(FX_LOG_##severity))

// Sets severity for global logger.
// |severity| is one of DEBUG, INFO, WARNING, ERROR, or FATAL.
#define FX_LOG_SET_SEVERITY(severity)                          \
  do {                                                         \
    fx_logger_t* logger = fx_log_get_logger();                 \
    if (logger) {                                              \
      fx_logger_set_min_severity(logger, (FX_LOG_##severity)); \
    }                                                          \
  } while (0);

// Writes a message to the global logger.
// |severity| is one of DEBUG, INFO, WARNING, ERROR, FATAL
// |tag| is a tag to associated with the message, or NULL if none.
// |message| is the message to write, or NULL if none.
#define FX_LOG(severity, tag, message)                                         \
  do {                                                                         \
    fx_logger_t* logger = fx_log_get_logger();                                 \
    if (logger && fx_logger_get_min_severity(logger) <= (FX_LOG_##severity)) { \
      fx_logger_log(logger, (FX_LOG_##severity), (tag), (message));            \
    }                                                                          \
  } while (0);

// Writes formatted message to the global logger.
// |severity| is one of DEBUG, INFO, WARNING, ERROR, FATAL
// |tag| is a tag to associated with the message, or NULL if none.
// |message| is the message to write, or NULL if none.
#define FX_LOGF(severity, tag, message, ...)                                   \
  do {                                                                         \
    fx_logger_t* logger = fx_log_get_logger();                                 \
    if (logger && fx_logger_get_min_severity(logger) <= (FX_LOG_##severity)) { \
      fx_logger_logf(logger, (FX_LOG_##severity), (tag), (message),            \
                     __VA_ARGS__);                                             \
    }                                                                          \
  } while (0);

// Writes formatted message to the global logger using vaargs
// |severity| is one of DEBUG, INFO, WARNING, ERROR, FATAL
// |tag| is a tag to associated with the message, or NULL if none.
// |message| is the message to write, or NULL if none.
// |args| are the arguments to |message|.
#define FX_LOGVF(severity, tag, message, args)                                 \
  do {                                                                         \
    fx_logger_t* logger = fx_log_get_logger();                                 \
    if (logger && fx_logger_get_min_severity(logger) <= (FX_LOG_##severity)) { \
      fx_logger_logvf(logger, (FX_LOG_##severity), (tag), (message), (args));  \
    }                                                                          \
  } while (0);

__END_CDECLS
