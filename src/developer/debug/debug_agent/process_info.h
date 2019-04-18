// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <zircon/syscalls/debug.h>
#include <zircon/types.h>

#include <vector>

#include "src/developer/debug/ipc/records.h"

struct zx_info_process;

namespace debug_agent {

zx_status_t GetProcessInfo(zx_handle_t process, zx_info_process* info);

// Fills the given vector with the module information for the process.
// "dl_debug_addr" is the address inside "process" of the dynamic loader's
// debug state.
zx_status_t GetModulesForProcess(const zx::process& process,
                                 uint64_t dl_debug_addr,
                                 std::vector<debug_ipc::Module>* modules);

// Returns the memory mapping for the process. Returns empty on failure.
std::vector<zx_info_maps_t> GetProcessMaps(const zx::process& process);

// Reads one memory block from the process. Returns block.valid for convenience.
bool ReadProcessMemoryBlock(const zx::process& process,
                            debug_ipc::MemoryBlock* block);

void ReadProcessMemoryBlocks(const zx::process& process, uint64_t address,
                             uint32_t size,
                             std::vector<debug_ipc::MemoryBlock>* blocks);

debug_ipc::ThreadRecord::State ThreadStateToEnums(
    uint32_t state, debug_ipc::ThreadRecord::BlockedReason* blocked_reason);

}  // namespace debug_agent
