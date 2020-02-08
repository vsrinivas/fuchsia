// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include <fbl/string.h>
#include <fbl/vector.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <lib/log/log.h>
#include <lib/log/log_writer.h>
#include <memory>
#include <stdio.h>

namespace {

// Configuration for a logger object.
// Specifies the destination to which log messages should be written.
typedef struct log_config {
  // The minimum log level.
  // Log messages with lower severity will be discarded.
  // If this number is negative, it refers to a verbosity.
  log_level_t min_level;

  // The writer that logs will go to.
  log_writer_t* log_writer;

  // An array of tag strings to associate with all messages written
  // by this logger.  Tags will be truncated if they are (individually) longer
  // than |LOG_MAX_TAG_LEN|.
  fbl::Vector<fbl::String> tags;
} log_config_t;

std::unique_ptr<log_config_t> g_log_config_ptr = std::unique_ptr<log_config_t>(nullptr);

void log_write_message_helper(log_level_t level, size_t num_tags, const char** tags_ptr,
                              const char* message, size_t message_len) {
  log_config_t* cfg = g_log_config_ptr.get();
  if (!cfg) {
    // Logging has not been initialized. Don't log anything.
    return;
  }

  fbl::Vector<const char*> static_tags;
  for (size_t i = 0; i < cfg->tags.size(); i++) {
    static_tags.push_back(cfg->tags[i].c_str());
  }

  log_message_t msg = {
      level,
      static_tags.data(),
      static_tags.size(),
      tags_ptr,
      num_tags,
      message,
      fbl::min(message_len, (size_t)LOG_MAX_MESSAGE_SIZE),
  };
  cfg->log_writer->ops->v1.write(cfg->log_writer, &msg);
}

}  // namespace

__EXPORT
bool log_level_is_enabled(log_level_t level) {
  log_config_t* cfg = g_log_config_ptr.get();
  return cfg && level >= cfg->min_level;
}

__EXPORT
void log_set_min_level(log_level_t level) {
  log_config_t* cfg = g_log_config_ptr.get();
  if (cfg) {
    cfg->min_level = level;
  }
}

__EXPORT
void log_initialize(log_level_t min_level, log_writer_t* log_writer, const size_t num_tags,
                    const char** tags) {
  if (g_log_config_ptr.get() != nullptr) {
    log_shutdown();
  }
  log_config_t* config = new log_config_t;
  config->min_level = min_level;
  config->log_writer = log_writer;

  ZX_ASSERT(num_tags <= LOG_MAX_TAGS);

  for (size_t i = 0; i < num_tags; i++) {
    config->tags.push_back(fbl::String(tags[i]));
  }

  g_log_config_ptr = std::unique_ptr<log_config_t>(config);
}

__EXPORT
void log_shutdown(void) { g_log_config_ptr = std::unique_ptr<log_config_t>(nullptr); }

__EXPORT
void log_write_message_printf(log_level_t level, size_t num_tags, const char** tags_ptr,
                              const char* format_string, ...) {
  char message[LOG_MAX_MESSAGE_SIZE];
  va_list args;
  va_start(args, format_string);
  int msg_len = vsnprintf(message, LOG_MAX_MESSAGE_SIZE, format_string, args);
  va_end(args);
  if (msg_len < 0) {
    // There was an error formatting. Toss the message.
    return;
  }
  log_write_message_helper(level, num_tags, tags_ptr, message, msg_len);
}

__EXPORT
void log_write_message(log_level_t level, size_t num_tags, const char** tags_ptr,
                       const char* message) {
  size_t message_len = strlen(message);
  log_write_message_helper(level, num_tags, tags_ptr, message, message_len);
}
