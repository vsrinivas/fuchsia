// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// This header contains definition for the logger object and protocol.

#ifndef ZIRCON_SYSTEM_ULIB_SYSLOG_INCLUDE_SYSLOG_LOGGER_H_
#define ZIRCON_SYSTEM_ULIB_SYSLOG_INCLUDE_SYSLOG_LOGGER_H_

#include <stdarg.h>
#include <unistd.h>

#include <zircon/types.h>

// Max no of tags associated with a logger.
#define FX_LOG_MAX_TAGS (4)

// Max individual tag length including terminating character.
#define FX_LOG_MAX_TAG_LEN (64)

// Log entry severity.
// Used for coarse filtering of log messages
typedef int fx_log_severity_t;
#define FX_LOG_INFO (0)
#define FX_LOG_WARNING (1)
#define FX_LOG_ERROR (2)
#define FX_LOG_FATAL (3)

__BEGIN_CDECLS

// Configuration for a logger object.
// Specifies the destination to which log messages should be written.
// Multiple destinations may be used concurrently.
typedef struct fx_logger_config {
    // The minimum log severity.
    // Log messages with lower severity will be discarded.
    fx_log_severity_t min_severity;

    // The file descriptor to which formatted log messages should be written,
    // or -1 if log messages should not be written to the console.
    // logger takes ownership of this fd.
    int console_fd;

    // The FIDL log service channel to which the logger should connect, or
    // |ZX_HANDLE_INVALID| if the logger should not connect to the log service.
    // logger takes ownership of this handle.
    zx_handle_t log_service_channel;

    // An array of tag strings to associate with all messages written
    // by this logger.  Tags will be truncated if they are (individually) longer
    // than |FX_LOG_MAX_TAG_LEN|.
    const char** tags;

    // Number of tag strings.  Must be no more than |FX_LOG_MAX_TAGS|.
    size_t num_tags;
} fx_logger_config_t;

// Opaque type representing a logger object.
typedef struct fx_logger fx_logger_t;

// Creates a logger object from the specified configuration.
//
// This will return ZX_ERR_INVALID_ARGS if |num_tags| is more than
// |FX_LOG_MAX_TAGS| and return |ZX_ERR_INTERNAL} if dup fails.
// |config| can be safely deleted after this function returns.
zx_status_t fx_logger_create(const fx_logger_config_t* config,
                             fx_logger_t** out_logger);

// Destroys a logger object.
//
// This closes |console_fd| or |log_service_channel| which were passed in
// |fx_logger_config_t|.
void fx_logger_destroy(fx_logger_t* logger);

// Gets the logger's minimum log severity.
fx_log_severity_t fx_logger_get_min_severity(fx_logger_t* logger);

// Sets logger severity
void fx_logger_set_min_severity(fx_logger_t* logger,
                                fx_log_severity_t severity);

// Activates fallback mode and logger starts writing to |fallback_fd|.
// There is no way to revert this action.
//
// This function does not take ownership of |fallback_fd| and it should not be
// closed till this logger object is no longer in use. Logger will log to
// stderr if -1 is provided.
//
// This function is thread unsafe.
void fx_logger_activate_fallback(fx_logger_t* logger,
                                 int fallback_fd);

// Writes formatted message to a logger.
// The message will be discarded if |severity| is less than the logger's
// minimum log severity.
// The |tag| may be NULL, in which case no additional tags are added to the
// log message.
// The |tag| will be truncated if it is longer than |FX_LOG_MAX_TAG_LEN|.
// No message is written if |message| is NULL.
zx_status_t fx_logger_logf(fx_logger_t* logger, fx_log_severity_t severity,
                           const char* tag, const char* msg, ...);

// Writes formatted message to a logger using varargs.
// The message will be discarded if |severity| is less than the logger's
// minimum log severity.
// The |tag| may be NULL, in which case no additional tags are added to the
// log message.
// The |tag| will be truncated if it is longer than |FX_LOG_MAX_TAG_LEN|.
// No message is written if |message| is NULL.
zx_status_t fx_logger_logvf(fx_logger_t* logger, fx_log_severity_t severity,
                            const char* tag, const char* msg, va_list args);

// Writes a message to a logger.
// The message will be discarded if |severity| is less than the logger's
// minimum log severity.
// The |tag| may be NULL, in which case no additional tags are added to the
// log message.
// The |tag| will be truncated if it is longer than |FX_LOG_MAX_TAG_LEN|.
// No message is written if |message| is NULL.
zx_status_t fx_logger_log(fx_logger_t* logger, fx_log_severity_t severity,
                          const char* tag, const char* msg);

__END_CDECLS

#endif // ZIRCON_SYSTEM_ULIB_SYSLOG_INCLUDE_SYSLOG_LOGGER_H_
