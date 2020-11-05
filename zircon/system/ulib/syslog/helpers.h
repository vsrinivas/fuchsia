// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_SYSLOG_HELPERS_H_
#define ZIRCON_SYSTEM_ULIB_SYSLOG_HELPERS_H_

#include <lib/syslog/logger.h>

namespace syslog {
namespace internal {

// Returns a string will all occurences of "../" at the beginning |path| removed.
const char* StripDots(const char* path);

// Extracts file name from |path|, which is the part after the last occurance of "/".
const char* StripPath(const char* path);

// Returns the full file path for |severity| higher than FX_LOG_INFO, otherwise returns the file
// name only.
const char* StripFile(const char* file, fx_log_severity_t severity);

}  // namespace internal
}  // namespace syslog

#endif  // ZIRCON_SYSTEM_ULIB_SYSLOG_HELPERS_H_
