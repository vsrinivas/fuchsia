// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/common/version.h"

// Generated file with BUILD_VERSION is created at build-time.
#include "src/developer/debug/zxdb/common/version.inc"

#ifndef BUILD_VERSION
#error "BUILD_VERSION must be defined"
#endif

namespace zxdb {

const char* kBuildVersion = BUILD_VERSION;

}  // namespace zxdb
