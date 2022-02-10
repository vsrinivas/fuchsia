// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_BRINGUP_BIN_SVCHOST_INCLUDE_CRASHSVC_LOGGING_H_
#define SRC_BRINGUP_BIN_SVCHOST_INCLUDE_CRASHSVC_LOGGING_H_

#include <zircon/status.h>
#include <zircon/syscalls/exception.h>

// Logs a general error unrelated to a particular exception.
void LogError(const char* message, zx_status_t status);

// Logs an error when handling the exception described by |info|.
void LogError(const char* message, const zx_exception_info& info);

// Logs an error when handling the exception described by |info|.
void LogError(const char* message, const zx_exception_info& info, zx_status_t status);

#endif  // SRC_BRINGUP_BIN_SVCHOST_INCLUDE_CRASHSVC_LOGGING_H_
