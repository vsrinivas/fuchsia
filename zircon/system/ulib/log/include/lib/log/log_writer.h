// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_LOG_LOG_WRITER_H_
#define LIB_LOG_LOG_WRITER_H_

#include <stdarg.h>
#include <zircon/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Log entry level.
// Used for coarse filtering of log messages
typedef int log_level_t;
#define LOG_LEVEL_VERBOSE(x) ((log_level_t) - (x))
#define LOG_LEVEL_INFO (0)
#define LOG_LEVEL_WARNING (1)
#define LOG_LEVEL_ERROR (2)
#define LOG_LEVEL_FATAL (3)

// Writer interface for emitting logs.
// There may be multiple implementations of this interface.
typedef struct log_writer log_writer_t;

typedef uint32_t log_ops_version_t;

#define LOG_WRITER_OPS_V1 ((log_ops_version_t)1)

// log_message exists to encode messages between the frontends and the backends
// (aka implementations of log_writer_t). After a message has been processed
// (e.g. printf formatting, streamed into a buffer, etc.), this struct is filled
// out and a pointer to it is then handed to the backend for dispatching.
typedef struct log_message {
  // The level of this message.
  log_level_t level;
  // List of tags that was provided when the logging frontend was initialized
  const char** static_tags;
  // The number of static tags
  const size_t num_static_tags;
  // List of tags that was provided for this log message in particular
  const char** dynamic_tags;
  // The number of dynamic tags
  const size_t num_dynamic_tags;
  // The body of this log message
  const char* text;
  // The length of the log message body
  // (NOT including the terminating null byte)
  const size_t text_len;
} log_message_t;

typedef struct log_writer_ops {
  // The interface version number, e.g. |LOG_OPS_V1|.
  log_ops_version_t version;

  // Reserved for future expansion, set to zero.
  uint32_t reserved;

  // Operations supported by |LOG_OPS_V1|.
  struct v1 {
    void (*write)(log_writer_t* writer, const log_message_t* message);
  } v1;
} log_writer_ops_t;

struct log_writer {
  const log_writer_ops_t* ops;
};

#ifdef __cplusplus
}
#endif

#endif  // LIB_LOG_LOG_WRITER_H_
