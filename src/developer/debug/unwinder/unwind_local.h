// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_UNWINDER_UNWIND_LOCAL_H_
#define SRC_DEVELOPER_DEBUG_UNWINDER_UNWIND_LOCAL_H_

#include "src/developer/debug/unwinder/unwind.h"

namespace unwinder {

// Unwind from the current location. The first frame in the returned value is the return address
// of this function call. This function is not available on macOS.
std::vector<Frame> UnwindLocal();

}  // namespace unwinder

#endif  // SRC_DEVELOPER_DEBUG_UNWINDER_UNWIND_LOCAL_H_
