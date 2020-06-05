// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_DEBUGGER_UTILS_UTIL_H_
#define GARNET_LIB_DEBUGGER_UTILS_UTIL_H_

#include <string>

#ifdef __Fuchsia__
#include <zircon/status.h>
#endif  // __Fuchsia__

namespace debugger_utils {

// Return a string representation of errno value |err|.
std::string ErrnoString(int err);

#ifdef __Fuchsia__
// Return a string representation of |status|.
// This includes both the numeric and text values.
std::string ZxErrorString(zx_status_t status);
#endif  // __Fuchsia__

}  // namespace debugger_utils

#endif  // GARNET_LIB_DEBUGGER_UTILS_UTIL_H_
