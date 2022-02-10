// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include <crashsvc/logging.h>

void LogError(const char* message, zx_status_t status) {
  fprintf(stderr, "crashsvc: %s: %s (%d)\n", message, zx_status_get_string(status), status);
}

void LogError(const char* message, const zx_exception_info& info) {
  fprintf(stderr, "crashsvc: %s [thread %" PRIu64 ".%" PRIu64 "]\n", message, info.pid, info.tid);
}

void LogError(const char* message, const zx_exception_info& info, zx_status_t status) {
  fprintf(stderr, "crashsvc: %s [thread %" PRIu64 ".%" PRIu64 "]: %s (%d)\n", message, info.pid,
          info.tid, zx_status_get_string(status), status);
}
