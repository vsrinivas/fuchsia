// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SLEEP_H_
#define SLEEP_H_

#include "magma_util/macros.h"
#include <chrono>
#include <thread>

namespace magma {

static inline void msleep(uint32_t ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

} // namespace magma

#endif
