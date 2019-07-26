// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_LOG_WRITER_TEXTFILE_LOG_WRITER_TEXTFILE_H_
#define LIB_LOG_WRITER_TEXTFILE_LOG_WRITER_TEXTFILE_H_

#include <lib/log/log_writer.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// log_create_textfile_writer will create a new log_writer_t that will write
// human readable text logs to the given destination. |log_destination| must be
// a valid opened file, whose ownership is handled by the caller.
//
// If the caller wishes to close |log_destination|, the
// log_destroy_textfile_writer function should be called on the returned
// log_writer_t first.
//
// This function will always return a valid, non-null pointer. A common value
// for |log_destination| is stderr.
log_writer_t* log_create_textfile_writer(FILE* log_destination);

// log_destroy_textfile_writer will free the memory used by a log_writer_t
// created by log_create_textfile_writer.
//
// This function should only be used on log_writer_t pointers returned by
// log_create_textfile_writer.
//
// This function will not close or otherwise modify the |log_destination| file
// pointer that was provided when the log_writer_t was created.
void log_destroy_textfile_writer(log_writer_t* writer);

#ifdef __cplusplus
}
#endif

#endif  // LIB_LOG_WRITER_TEXTFILE_LOG_WRITER_TEXTFILE_H_
