// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/status.h>

#include <stdint.h>

#include <functional>
#include <vector>

namespace debug_ipc {
struct Module;
}

namespace debug_agent {

class ProcessHandle;

// Iterates through all modules in the given process, calling the callback for each. The callback
// should return true to keep iterating, false to stop now.
zx_status_t WalkElfModules(const ProcessHandle& process, uint64_t dl_debug_addr,
                           std::function<bool(uint64_t base_addr, uint64_t lmap)> cb);

// Computes the modules for the given process.
std::vector<debug_ipc::Module> GetElfModulesForProcess(const ProcessHandle& process,
                        uint64_t dl_debug_addr);

}  // namespace debug_agent
