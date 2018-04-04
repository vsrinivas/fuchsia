// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <vector>
#include <zx/thread.h>

namespace debug_agent {

zx_status_t UnwindStack(const zx::process& process,
                        const zx::thread& thread,
                        uint64_t ip,
                        uint64_t sp,
                        size_t max_depth,
                        std::vector<uint64_t>* stack);

}  // namespace debug_agent
