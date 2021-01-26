// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_LIB_UTILS_USE_DEBUG_LOG_H_
#define SRC_STORAGE_LIB_UTILS_USE_DEBUG_LOG_H_

#include <lib/zx/channel.h>
#include <lib/zx/status.h>

#include <string>
#include <string_view>

namespace storage {

// Opens handle to log write service and reconfigures syslog to use that
// handle for logging. This is a short term fix for a bug where in on a
// board with userdebug build, no logs show up on serial.
// TODO(fxbug.dev/66476)
void UseDebugLog(const std::string& tag);

}  // namespace storage

#endif  // SRC_STORAGE_LIB_UTILS_USE_DEBUG_LOG_H_
