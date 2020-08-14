// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_SYS_TOOLS_LOG_LOG_H_
#define SRC_SYS_TOOLS_LOG_LOG_H_

#include <fuchsia/logger/cpp/fidl.h>

namespace log {

zx_status_t ParseAndWriteLog(fuchsia::logger::LogSinkHandle log_sink, zx::time time, int argc,
                             char** argv);

}  // namespace log

#endif  // SRC_SYS_TOOLS_LOG_LOG_H_
