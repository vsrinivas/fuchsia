// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FXL_LOGGING_H_
#define SRC_LIB_FXL_LOGGING_H_

#include <lib/syslog/cpp/macros.h>

#include "src/lib/fxl/log_settings.h"

// This include only exists because some clients get the FXL macros through
// this file instead of macros.h directly.
// TODO(samans): Fix all said clients and remove this include.
#include "src/lib/fxl/macros.h"

namespace fxl {

using syslog::GetVlogVerbosity;
using syslog::LOG_DFATAL;
using syslog::LOG_ERROR;
using syslog::LOG_FATAL;
using syslog::LOG_INFO;
using syslog::LOG_LEVEL;
using syslog::LOG_WARNING;
using syslog::LogSeverity;
using syslog::ShouldCreateLogMessage;

}  // namespace fxl

#endif  // SRC_LIB_FXL_LOGGING_H_
