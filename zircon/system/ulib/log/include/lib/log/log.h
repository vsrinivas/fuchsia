// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_LOG_LOG_H_
#define LIB_LOG_LOG_H_

#include <lib/log/log_writer.h>

#ifdef __cplusplus
extern "C" {
#endif

// The values following constants must match the values defined in
// //zircon/system/fidl/fuchsia-logger/logger.fidl

// Max number of tags that can be attached to a message.
#define LOG_MAX_TAGS (5)

// Max individual tag length including terminating character.
#define LOG_MAX_TAG_LEN (63)

// Max individual message length including terminating character
#define LOG_MAX_MESSAGE_SIZE (2032)

// Returns true if writing messages with the given level is enabled in the
// global logger.
// |level| is one of VERBOSE(N), INFO, WARNING, ERROR, or FATAL.
#define LOG_LEVEL_IS_ENABLED(level) log_level_is_enabled(LOG_LEVEL_##level)

// Sets the minimum level for global logger. Log messages with a lower severity
// (or higher verbosity) than the given value will not be emitted. |level| is
// one of VERBOSE(N), INFO, WARNING, ERROR, or FATAL.
#define LOG_SET_MIN_LEVEL(level) log_set_min_level(LOG_LEVEL_##level)

// Initializes the logging system. Can be called multiple times.
//
// Arguments:
//   |min_level| - one of VERBOSE(int), INFO, WARNING, ERROR, FATAL
//   |log_writer| - a log_writer_t* to use when writing logs
//   |...| - tags to be attached to all messages
//
// Example usage:
//   LOG_INITIALIZE(INFO, log_writer, "program_name");
#define LOG_INITIALIZE(min_level, log_writer, ...)                     \
  do {                                                                 \
    const char* tags[] = {__VA_ARGS__};                                \
    size_t num_tags = sizeof(tags) / sizeof(tags[0]);                  \
    log_initialize(LOG_LEVEL_##min_level, log_writer, num_tags, tags); \
  } while (0)

// Frees and resets the global logging state. Should be called at program end to
// clean up memory taken up by this logging framework. If messages are logged
// after log_shutdown has been called the messages are silently dropped.
void log_shutdown(void);

// Log a formatted message at the given level with the given tags.
// Takes two sets of arguments.
//
// First set of arguments:
//   |...| - tags to be attached to the message
// Second set of arguments:
//   |level| - one of VERBOSE(int), INFO, WARNING, ERROR, FATAL
//   |format_string| - the format string for the message
//   |...| - arguments to be filled in to the format string
//
// Example usage:
//   LOGF(ERROR, "tag1", "tag2")("sorry that didn't work: %s", msg);
#define LOGF(level, ...)                              \
  do {                                                \
    const char* tags[] = {__VA_ARGS__};               \
    size_t num_tags = sizeof(tags) / sizeof(tags[0]); \
    log_level_t lvl = LOG_LEVEL_##level;              \
  LOGF_INNER

// Log a message at the given level with the given tags.
// Takes two sets of arguments.
//
// First set of arguments:
//   |...| - tags to be attached to the message
// Second set of arguments:
//   |level| - one of VERBOSE(int), INFO, WARNING, ERROR, FATAL
//   |message| - the message to log
//
// Example usage:
//   LOG(ERROR, "tag1", "tag2")("internal error encountered");
#define LOG(level, ...)                               \
  do {                                                \
    const char* tags[] = {__VA_ARGS__};               \
    size_t num_tags = sizeof(tags) / sizeof(tags[0]); \
    log_level_t lvl = LOG_LEVEL_##level;              \
  LOG_INNER

// The following are helper functions and macros used in the above macros, and
// should not be called directly outside of macro usage.

#define LOGF_INNER(message, ...)                                         \
  if (log_level_is_enabled(lvl)) {                                       \
    log_write_message_printf(lvl, num_tags, tags, message, __VA_ARGS__); \
  }                                                                      \
  }                                                                      \
  while (0)

#define LOG_INNER(message)                           \
  if (log_level_is_enabled(lvl)) {                   \
    log_write_message(lvl, num_tags, tags, message); \
  }                                                  \
  }                                                  \
  while (0)

bool log_level_is_enabled(log_level_t level);

void log_set_min_level(log_level_t min_level);

void log_initialize(log_level_t min_level, log_writer_t* log_writer, const size_t num_tags,
                    const char** tags);

void log_write_message(log_level_t level, size_t num_tags, const char** tags_ptr,
                       const char* message);

void log_write_message_printf(log_level_t level, size_t num_tags, const char** tags_ptr,
                              const char* message, ...);

#ifdef __cplusplus
}
#endif

#endif  // LIB_LOG_LOG_H_
