// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_LOG_LOG_H_
#define SRC_DEVICES_LIB_LOG_LOG_H_

#include <lib/syslog/global.h>

#define LOGF(severity, message...) FX_LOGF(severity, nullptr, message)
#define VLOGF(verbosity, message...) FX_VLOGF(verbosity, nullptr, message)

// Redirect log statements from syslog to debuglog.
zx_status_t log_to_debuglog();

#endif  // SRC_DEVICES_LIB_LOG_LOG_H_
