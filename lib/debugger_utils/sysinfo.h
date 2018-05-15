// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_DEBUGGER_UTILS_SYSINFO_H_
#define GARNET_LIB_DEBUGGER_UTILS_SYSINFO_H_

#include <lib/zx/job.h>

namespace debugserver {
namespace util {

zx::job GetRootJob();

}  // namespace util
}  // namespace debugserver

#endif  // GARNET_LIB_DEBUGGER_UTILS_SYSINFO_H_
