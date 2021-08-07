// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_SHARED_ARCH_H_
#define SRC_DEVELOPER_DEBUG_SHARED_ARCH_H_

#include <inttypes.h>

namespace debug {

enum class Arch : uint32_t { kUnknown = 0, kX64, kArm64 };

}  // namespace debug

#endif  // SRC_DEVELOPER_DEBUG_SHARED_ARCH_H_
