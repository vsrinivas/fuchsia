// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PLATFORM_THREAD_H
#define PLATFORM_THREAD_H

#include <stdint.h>

namespace magma {

// Use std::thread except for ids.
class PlatformThreadId {
public:
    PlatformThreadId() { SetToCurrent(); }

    uint32_t id() { return id_; }

    void SetToCurrent()
    {
        id_ = GetCurrentThreadId();
    }

    bool IsCurrent() { return id_ == GetCurrentThreadId(); }

private:
    static uint32_t GetCurrentThreadId();

    uint32_t id_ = 0;
};

} // namespace magma

#endif // PLATFORM_THREAD_H
