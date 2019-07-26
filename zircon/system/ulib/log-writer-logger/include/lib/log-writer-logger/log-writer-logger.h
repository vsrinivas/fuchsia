// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_LOG_WRITER_LOGGER_LOG_WRITER_LOGGER_H_
#define LIB_LOG_WRITER_LOGGER_LOG_WRITER_LOGGER_H_

#include <lib/log/log_writer.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// log_create_logger_writer will create a new log_writer_t that will write
// log messages to the fuchsia.logger.LogSink FIDL service. These logs are then
// accessible from the fuchsia.logger.Log FIDL service. The log_listener program
// in garnet exists as a CLI frontend to this service.
//
// This function will always return a valid, non-null pointer.
log_writer_t* log_create_logger_writer(void);

// log_destroy_logger_writer will free the memory used by a log_writer_t
// created by log_create_logger_writer.
//
// This function should only be used on log_writer_t pointers returned by
// log_create_logger_writer.
void log_destroy_logger_writer(log_writer_t* writer);

// log_set_logger_writer_socket will replace the socket handle negotiated during
// the log_create_logger_writer function with the provided value, causing log
// messages to be written into the given socket instead of the socket to logger.
//
// This is only provided for debugging purposes.
void log_set_logger_writer_socket(log_writer_t* writer, zx_handle_t socket);

#ifdef __cplusplus
}
#endif

#endif  // LIB_LOG_WRITER_LOGGER_LOG_WRITER_LOGGER_H_
