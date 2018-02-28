// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Entry points used by clients.

#ifndef ZIRCON_SYSTEM_ULIB_SYSLOG_INCLUDE_SYSLOG_GLOBAL_H_
#define ZIRCON_SYSTEM_ULIB_SYSLOG_INCLUDE_SYSLOG_GLOBAL_H_

#include "logger.h"

__BEGIN_CDECLS

// Gets the global logger for the process to which log messages emitted
// using the FX_LOG macros will be written.  Returns NULL if logging is
// not configured.
fx_logger_t* fx_log_get_logger(void);

// Returns true if writing messages with the given severity is enabled in the
// global logger.
static inline bool fx_log_is_enabled(fx_log_severity_t severity) {
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

// Returns true if writing messages with the given verbosity is enabled in the
// global logger. |verbosity| is positive number.
#define FX_VLOG_IS_ENABLED(verbosity) (fx_log_is_enabled(-(verbosity)))

#define _FX_LOG_SET_SEVERITY(severity)                      \
    do {                                                    \
        fx_logger_t* logger = fx_log_get_logger();          \
        if (logger) {                                       \
            fx_logger_set_min_severity(logger, (severity)); \
        }                                                   \
    } while (0)

// Sets severity for global logger.
// |severity| is one of DEBUG, INFO, WARNING, ERROR, or FATAL.
#define FX_LOG_SET_SEVERITY(severity) _FX_LOG_SET_SEVERITY(FX_LOG_##severity)

// Sets verbosity for global logger.
// |verbosity| is positive number. Logger severity is set to -verbosity
#define FX_LOG_SET_VERBOSITY(verbosity) _FX_LOG_SET_SEVERITY(-(verbosity))

#define _FX_LOG(severity, tag, message)                                   \
    do {                                                                  \
        fx_logger_t* logger = fx_log_get_logger();                        \
        if (logger && fx_logger_get_min_severity(logger) <= (severity)) { \
            fx_logger_log(logger, (severity), (tag), (message));          \
        }                                                                 \
    } while (0)

#define _FX_LOGF(severity, tag, message, ...)                             \
    do {                                                                  \
        fx_logger_t* logger = fx_log_get_logger();                        \
        if (logger && fx_logger_get_min_severity(logger) <= (severity)) { \
            fx_logger_logf(logger, (severity), (tag), (message),          \
                           __VA_ARGS__);                                  \
        }                                                                 \
    } while (0)

#define _FX_LOGVF(severity, tag, message, args)                            \
    do {                                                                   \
        fx_logger_t* logger = fx_log_get_logger();                         \
        if (logger && fx_logger_get_min_severity(logger) <= (severity)) {  \
            fx_logger_logvf(logger, (severity), (tag), (message), (args)); \
        }                                                                  \
    } while (0)

// Writes a message to the global logger.
// |severity| is one of DEBUG, INFO, WARNING, ERROR, FATAL
// |tag| is a tag to associated with the message, or NULL if none.
// |message| is the message to write, or NULL if none.
#define FX_LOG(severity, tag, message) _FX_LOG((FX_LOG_##severity), tag, message)

// Writes formatted message to the global logger.
// |severity| is one of DEBUG, INFO, WARNING, ERROR, FATAL
// |tag| is a tag to associated with the message, or NULL if none.
// |message| is the message to write, or NULL if none.
#define FX_LOGF(severity, tag, message, ...) _FX_LOGF((FX_LOG_##severity), tag, message, __VA_ARGS__)

// Writes formatted message to the global logger using vaargs
// |severity| is one of DEBUG, INFO, WARNING, ERROR, FATAL
// |tag| is a tag to associated with the message, or NULL if none.
// |message| is the message to write, or NULL if none.
// |args| are the arguments to |message|.
#define FX_LOGVF(severity, tag, message, args) _FX_LOGVF((FX_LOG_##severity), tag, message, args)

// Writes verbose message to the global logger.
// |verbosity| is positive integer.
// |tag| is a tag to associated with the message, or NULL if none.
// |message| is the message to write, or NULL if none.
#define FX_VLOG(verbosity, tag, message) _FX_LOG(-(verbosity), tag, message)

// Writes formatted verbose message to the global logger.
// |verbosity| is positive integer.
// |tag| is a tag to associated with the message, or NULL if none.
// |message| is the message to write, or NULL if none.
#define FX_VLOGF(verbosity, tag, message, ...) _FX_LOGF(-(verbosity), tag, message, __VA_ARGS__)

// Writes formatted verbose message to the global logger using vaargs
// |verbosity| is positive integer.
// |tag| is a tag to associated with the message, or NULL if none.
// |message| is the message to write, or NULL if none.
// |args| are the arguments to |message|.
#define FX_VLOGVF(verbosity, tag, message, args) _FX_LOGVF(-(verbosity), tag, message, args)

__END_CDECLS

#endif // ZIRCON_SYSTEM_ULIB_SYSLOG_INCLUDE_SYSLOG_GLOBAL_H_
