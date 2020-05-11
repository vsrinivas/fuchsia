// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// This header contains definition for the logger object and protocol.

#ifndef LIB_SYSLOG_LOGGER_H_
#define LIB_SYSLOG_LOGGER_H_

#include <stdarg.h>
#include <unistd.h>
#include <zircon/types.h>

// Max no of tags associated with a logger.
#define FX_LOG_MAX_TAGS (4)

// Max individual tag length including terminating character.
#define FX_LOG_MAX_TAG_LEN (64)

// Step size between discrete values that define log severity.
#define FX_LOG_SEVERITY_STEP_SIZE (16)

// Step size between discrete values that define log verbosity.
#define FX_LOG_VERBOSITY_STEP_SIZE (1)

// Log entry severity. Used for coarse filtering of log messages.
typedef int fx_log_severity_t;
#define FX_LOG_ALL (-0x7F)
#define FX_LOG_TRACE (0x10)    // 1 * FX_LOG_SEVERITY_STEP_SIZE
#define FX_LOG_DEBUG (0x20)    // 2 * FX_LOG_SEVERITY_STEP_SIZE
#define FX_LOG_INFO (0x30)     // 3 * FX_LOG_SEVERITY_STEP_SIZE
#define FX_LOG_WARNING (0x40)  // 4 * FX_LOG_SEVERITY_STEP_SIZE
#define FX_LOG_ERROR (0x50)    // 5 * FX_LOG_SEVERITY_STEP_SIZE
#define FX_LOG_FATAL (0x60)    // 6 * FX_LOG_SEVERITY_STEP_SIZE
#define FX_LOG_NONE (0x7F)

#define FX_LOG_SEVERITY_MAX (6)  // FX_LOG_FATAL

// Default log severity used in the standard logger config. To create a
// build that uses a higher/lower level of severity as the threshold
// for printed logs, redefine this on the command line.
#define FX_LOG_SEVERITY_DEFAULT (FX_LOG_INFO)

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

  // One end of the socket that goes to the log service. |ZX_HANDLE_INVALID| if
  // logs should not go to the log service.
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
// |FX_LOG_MAX_TAGS|.
// |config| can be safely deleted after this function returns.
zx_status_t fx_logger_create(const fx_logger_config_t* config, fx_logger_t** out_logger);

// Destroys a logger object.
//
// This closes |console_fd| or |log_service_channel| which were passed in
// |fx_logger_config_t|.
void fx_logger_destroy(fx_logger_t* logger);

// Gets the logger's minimum log severity.
fx_log_severity_t fx_logger_get_min_severity(fx_logger_t* logger);

// Sets the logger's minimum log severity.
zx_status_t fx_logger_set_min_severity(fx_logger_t* logger, fx_log_severity_t severity);

// Activates fallback mode and logger starts writing to |fallback_fd|.
// There is no way to revert this action.
//
// This function does not take ownership of |fallback_fd| and it should not be
// closed till this logger object is no longer in use. Logger will log to
// stderr if -1 is provided.
//
// This function is thread unsafe.
void fx_logger_activate_fallback(fx_logger_t* logger, int fallback_fd);

// Writes formatted message to a logger.
// The message will be discarded if |severity| is less than the logger's
// minimum log severity.
// The |tag| may be NULL, in which case no additional tags are added to the
// log message.
// The |tag| will be truncated if it is longer than |FX_LOG_MAX_TAG_LEN|.
// No message is written if |message| is NULL.
zx_status_t fx_logger_logf(fx_logger_t* logger, fx_log_severity_t severity, const char* tag,
                           const char* msg, ...);

// Writes formatted message to a logger using varargs.
// The message will be discarded if |severity| is less than the logger's
// minimum log severity.
// The |tag| may be NULL, in which case no additional tags are added to the
// log message.
// The |tag| will be truncated if it is longer than |FX_LOG_MAX_TAG_LEN|.
// No message is written if |message| is NULL.
zx_status_t fx_logger_logvf(fx_logger_t* logger, fx_log_severity_t severity, const char* tag,
                            const char* msg, va_list args);

// Writes a message to a logger.
// The message will be discarded if |severity| is less than the logger's
// minimum log severity.
// The |tag| may be NULL, in which case no additional tags are added to the
// log message.
// The |tag| will be truncated if it is longer than |FX_LOG_MAX_TAG_LEN|.
// No message is written if |message| is NULL.
zx_status_t fx_logger_log(fx_logger_t* logger, fx_log_severity_t severity, const char* tag,
                          const char* msg);

__END_CDECLS

#endif  // LIB_SYSLOG_LOGGER_H_
