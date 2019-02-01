// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_THREAD_H_
#define GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_THREAD_H_

#include "platform_thread.h"

namespace magma {

class ThreadIdCheck {
public:
    static bool IsCurrent(PlatformThreadId& thread_id) { return thread_id.IsCurrent(); }
};

} // namespace magma

#endif // GARNET_LIB_MAGMA_SRC_MAGMA_UTIL_THREAD_H_
